#include <OpenModel/ProjectArchive.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <string>
#include <system_error>
#include <vector>
#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#endif

/*
 * ZIP with stored entries only (method 0). Local headers carry the CRC and both sizes, so an entry is written in two
 * passes: first its size and CRC are computed, then the header and the bytes are written. The reader accepts only
 * this layout: no compression, no encryption, no data descriptors, no ZIP64, no archive comment, no multi-disk.
 */

namespace fs = std::filesystem;

namespace OpenModel {
namespace {

namespace zip {
constexpr std::uint32_t kLocalSig = 0x04034b50u;
constexpr std::uint32_t kCentralSig = 0x02014b50u;
constexpr std::uint32_t kEndSig = 0x06054b50u;
constexpr std::uint16_t kVersion = 20;          // 2.0: the version needed for stored entries with UTF-8 names
constexpr std::uint16_t kFlagEncrypted = 0x0001;
constexpr std::uint16_t kFlagDataDescriptor = 0x0008;
constexpr std::uint16_t kFlagUtf8 = 0x0800;
constexpr std::uint16_t kDosTime = 0x0000;      // 00:00:00, fixed so that equal inputs give equal archives
constexpr std::uint16_t kDosDate = 0x0021;      // 1980-01-01
constexpr std::size_t kLocalSize = 30;
constexpr std::size_t kCentralSize = 46;
constexpr std::size_t kEndSize = 22;
constexpr std::size_t kMaxEntries = 0xFFFE;     // 0xFFFF is the ZIP64 marker
constexpr std::uint64_t kMax32 = 0xFFFFFFFFull;
} // namespace zip

constexpr std::size_t kChunk = 1u << 16;

std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        table[i] = c;
    }
    return table;
}
const auto kCrcTable = makeCrcTable();

std::uint32_t crcUpdate(std::uint32_t crc, const char* data, std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i)
        crc = kCrcTable[(crc ^ static_cast<std::uint8_t>(data[i])) & 0xFFu] ^ (crc >> 8);
    return crc;
}

void put16(std::string& b, std::uint16_t v) {
    b.push_back(static_cast<char>(v & 0xFFu));
    b.push_back(static_cast<char>(v >> 8));
}
void put32(std::string& b, std::uint32_t v) {
    put16(b, static_cast<std::uint16_t>(v & 0xFFFFu));
    put16(b, static_cast<std::uint16_t>(v >> 16));
}
std::uint16_t get16(const std::string& b, std::size_t pos) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[pos]) |
                                      (static_cast<std::uint16_t>(static_cast<std::uint8_t>(b[pos + 1])) << 8));
}
std::uint32_t get32(const std::string& b, std::size_t pos) {
    return static_cast<std::uint32_t>(get16(b, pos)) | (static_cast<std::uint32_t>(get16(b, pos + 2)) << 16);
}

bool isSafeComponent(const std::string& part) {
    if (part.empty() || part == "." || part == "..") return false;
    for (const char c : part) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || c == '/' || c == '\\' || c == ':') return false;
    }
    return true;
}

// Entry names are relative, use '/' only, and never leave the destination.
bool isSafeEntryName(const std::string& name) {
    if (name.empty() || name.size() > 0xFFFF || name.front() == '/' || name.back() == '/') return false;
    std::size_t start = 0;
    while (start <= name.size()) {
        const auto slash = name.find('/', start);
        const auto end = slash == std::string::npos ? name.size() : slash;
        const auto part = name.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        for (const char c : part)
            if (static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == ':') return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

fs::path tempSibling(const fs::path& target, const char* tag) {
    const auto nonce = static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count());
    return fs::path(target.string() + tag + ".tmp." + std::to_string(nonce));
}

void replaceFile(const fs::path& from, const fs::path& to) {
#if defined(_WIN32)
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw ProjectArchiveError("cannot replace " + to.string());
#else
    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) throw ProjectArchiveError("cannot replace " + to.string() + ": " + ec.message());
#endif
}

std::string readAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ProjectArchiveError("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

struct Pending {
    std::string name;
    fs::path file;        // empty when the entry is in memory
    std::string bytes;    // in-memory payload
    std::uint64_t size = 0;
    std::uint32_t crc = 0;
};

// Streams a file through the CRC; the second pass copies the same bytes, so a file changed in between is detected.
void fileCrc(const fs::path& path, std::uint64_t& size, std::uint32_t& crc) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ProjectArchiveError("cannot read " + path.string());
    std::string buffer(kChunk, '\0');
    std::uint32_t c = 0xFFFFFFFFu;
    size = 0;
    while (in) {
        in.read(&buffer[0], static_cast<std::streamsize>(buffer.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        c = crcUpdate(c, buffer.data(), got);
        size += got;
    }
    crc = c ^ 0xFFFFFFFFu;
}

} // namespace

void exportProjectArchive(const fs::path& manifest, const fs::path& archive) {
    try {
        const auto project = OpenModelProject::load(manifest);
        const auto issues = project.verifyAssetIntegrity(manifest);
        if (!issues.empty())
            throw ProjectArchiveError("archive refused, asset '" + issues.front().assetId + "': " + issues.front().message);

        // Every asset becomes a managed entry under assets/<id>/<file name>; linked files are copied in.
        OpenModelProject archived;
        archived.metadata = project.metadata;
        std::vector<Pending> pending;
        pending.push_back({kProjectArchiveManifestName, {}, {}, 0, 0}); // manifest bytes are filled in below

        for (const auto& asset : project.assets) {
            if (!isSafeComponent(asset.id)) throw ProjectArchiveError("asset ID cannot be stored in an archive: " + asset.id);
            const auto source = project.resolveAssetPath(manifest, asset);
            if (!fs::is_regular_file(source)) throw ProjectArchiveError("asset file is missing: " + source.string());
            const auto fileName = asset.path.filename().string();
            if (!isSafeComponent(fileName)) throw ProjectArchiveError("asset file name cannot be stored: " + fileName);

            ProjectAsset copy = asset;
            const auto entryName = "assets/" + asset.id + "/" + fileName;
            copy.path = entryName;
            copy.storage = ProjectAssetStorage::Managed;
            archived.addAsset(copy);
            Pending entry;
            entry.name = entryName;
            entry.file = source;
            pending.push_back(std::move(entry));
        }
        if (pending.size() > zip::kMaxEntries) throw ProjectArchiveError("archive would need ZIP64, which is not supported");

        const auto manifestTemp = tempSibling(archive, ".manifest");
        try { archived.save(manifestTemp); }
        catch (const std::exception& error) {
            std::error_code ignored;
            fs::remove(manifestTemp, ignored);
            throw ProjectArchiveError(error.what());
        }
        pending.front().bytes = readAll(manifestTemp);
        pending.front().size = pending.front().bytes.size();
        pending.front().crc = 0;
        {
            std::uint32_t c = 0xFFFFFFFFu;
            c = crcUpdate(c, pending.front().bytes.data(), pending.front().bytes.size());
            pending.front().crc = c ^ 0xFFFFFFFFu;
        }
        std::error_code ignored;
        fs::remove(manifestTemp, ignored);

        for (std::size_t i = 1; i < pending.size(); ++i) fileCrc(pending[i].file, pending[i].size, pending[i].crc);

        const auto parent = archive.parent_path().empty() ? fs::current_path() : archive.parent_path();
        fs::create_directories(parent);
        const auto temporary = tempSibling(archive, "");
        try {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            if (!out) throw ProjectArchiveError("cannot create " + temporary.string());

            struct Central { std::string name; std::uint32_t crc; std::uint32_t size; std::uint32_t offset; };
            std::vector<Central> directory;
            std::uint64_t offset = 0;
            for (auto& entry : pending) {
                if (!isSafeEntryName(entry.name)) throw ProjectArchiveError("unsafe archive entry name: " + entry.name);
                const auto headerBytes = zip::kLocalSize + entry.name.size();
                if (offset + headerBytes + entry.size > zip::kMax32 || offset > zip::kMax32)
                    throw ProjectArchiveError("archive would need ZIP64, which is not supported");

                std::string h;
                put32(h, zip::kLocalSig);
                put16(h, zip::kVersion);
                put16(h, zip::kFlagUtf8);
                put16(h, 0); // method: stored
                put16(h, zip::kDosTime);
                put16(h, zip::kDosDate);
                put32(h, entry.crc);
                put32(h, static_cast<std::uint32_t>(entry.size));
                put32(h, static_cast<std::uint32_t>(entry.size));
                put16(h, static_cast<std::uint16_t>(entry.name.size()));
                put16(h, 0); // extra field length
                h += entry.name;
                out.write(h.data(), static_cast<std::streamsize>(h.size()));
                directory.push_back({entry.name, entry.crc, static_cast<std::uint32_t>(entry.size),
                                     static_cast<std::uint32_t>(offset)});

                if (entry.file.empty()) {
                    out.write(entry.bytes.data(), static_cast<std::streamsize>(entry.bytes.size()));
                } else {
                    std::ifstream in(entry.file, std::ios::binary);
                    if (!in) throw ProjectArchiveError("cannot read " + entry.file.string());
                    std::string buffer(kChunk, '\0');
                    std::uint64_t copied = 0;
                    std::uint32_t c = 0xFFFFFFFFu;
                    while (in) {
                        in.read(&buffer[0], static_cast<std::streamsize>(buffer.size()));
                        const auto got = static_cast<std::size_t>(in.gcount());
                        c = crcUpdate(c, buffer.data(), got);
                        out.write(buffer.data(), static_cast<std::streamsize>(got));
                        copied += got;
                    }
                    if (copied != entry.size || (c ^ 0xFFFFFFFFu) != entry.crc)
                        throw ProjectArchiveError("asset changed while it was being archived: " + entry.file.string());
                }
                offset += headerBytes + entry.size;
            }

            const auto directoryOffset = offset;
            std::uint64_t directorySize = 0;
            for (const auto& item : directory) {
                std::string c;
                put32(c, zip::kCentralSig);
                put16(c, zip::kVersion);   // made by
                put16(c, zip::kVersion);   // needed
                put16(c, zip::kFlagUtf8);
                put16(c, 0);
                put16(c, zip::kDosTime);
                put16(c, zip::kDosDate);
                put32(c, item.crc);
                put32(c, item.size);
                put32(c, item.size);
                put16(c, static_cast<std::uint16_t>(item.name.size()));
                put16(c, 0); // extra
                put16(c, 0); // comment
                put16(c, 0); // disk
                put16(c, 0); // internal attributes
                put32(c, 0); // external attributes
                put32(c, item.offset);
                c += item.name;
                out.write(c.data(), static_cast<std::streamsize>(c.size()));
                directorySize += c.size();
            }
            if (directoryOffset > zip::kMax32 || directorySize > zip::kMax32)
                throw ProjectArchiveError("archive would need ZIP64, which is not supported");

            std::string e;
            put32(e, zip::kEndSig);
            put16(e, 0);
            put16(e, 0);
            put16(e, static_cast<std::uint16_t>(directory.size()));
            put16(e, static_cast<std::uint16_t>(directory.size()));
            put32(e, static_cast<std::uint32_t>(directorySize));
            put32(e, static_cast<std::uint32_t>(directoryOffset));
            put16(e, 0);
            out.write(e.data(), static_cast<std::streamsize>(e.size()));

            out.flush();
            if (!out) throw ProjectArchiveError("archive write failed: " + temporary.string());
            out.close();
            if (!out) throw ProjectArchiveError("archive close failed: " + temporary.string());
            replaceFile(temporary, archive);
        } catch (...) {
            std::error_code ec;
            fs::remove(temporary, ec);
            throw;
        }
    } catch (const ProjectArchiveError&) {
        throw;
    } catch (const std::exception& error) {
        throw ProjectArchiveError(error.what());
    }
}

fs::path importProjectArchive(const fs::path& archive, const fs::path& destination) {
    struct Entry { std::string name; std::uint16_t flags; std::uint32_t crc; std::uint32_t size; std::uint32_t offset; };
    std::vector<fs::path> written;
    bool createdDestination = false;
    try {
        std::ifstream in(archive, std::ios::binary);
        if (!in) throw ProjectArchiveError("cannot open archive: " + archive.string());
        in.seekg(0, std::ios::end);
        const auto fileSize = static_cast<std::uint64_t>(in.tellg());
        if (fileSize < zip::kEndSize) throw ProjectArchiveError("archive is too small to be a ZIP file");

        in.seekg(static_cast<std::streamoff>(fileSize - zip::kEndSize));
        std::string end(zip::kEndSize, '\0');
        in.read(&end[0], static_cast<std::streamsize>(end.size()));
        if (!in || get32(end, 0) != zip::kEndSig)
            throw ProjectArchiveError("not a ZIP archive, or it has an archive comment (not supported)");
        const auto disk = get16(end, 4), diskOfDirectory = get16(end, 6);
        const auto entriesOnDisk = get16(end, 8), totalEntries = get16(end, 10);
        const auto directorySize = get32(end, 12), directoryOffset = get32(end, 16);
        if (disk != 0 || diskOfDirectory != 0 || entriesOnDisk != totalEntries)
            throw ProjectArchiveError("multi-disk archives are not supported");
        if (totalEntries == 0xFFFF || directorySize == 0xFFFFFFFFu || directoryOffset == 0xFFFFFFFFu)
            throw ProjectArchiveError("ZIP64 archives are not supported");
        if (static_cast<std::uint64_t>(directoryOffset) + directorySize != fileSize - zip::kEndSize)
            throw ProjectArchiveError("unexpected data between the central directory and the end record");

        in.seekg(directoryOffset);
        std::string directory(directorySize, '\0');
        in.read(&directory[0], static_cast<std::streamsize>(directory.size()));
        if (!in) throw ProjectArchiveError("central directory is truncated");

        std::vector<Entry> entries;
        std::set<std::string> names;
        std::size_t pos = 0;
        for (std::uint16_t i = 0; i < totalEntries; ++i) {
            if (pos + zip::kCentralSize > directory.size()) throw ProjectArchiveError("central directory is truncated");
            if (get32(directory, pos) != zip::kCentralSig) throw ProjectArchiveError("bad central directory entry");
            const auto flags = get16(directory, pos + 8);
            const auto method = get16(directory, pos + 10);
            const auto crc = get32(directory, pos + 16);
            const auto compressed = get32(directory, pos + 20);
            const auto uncompressed = get32(directory, pos + 24);
            const auto nameLength = get16(directory, pos + 28);
            const auto extraLength = get16(directory, pos + 30);
            const auto commentLength = get16(directory, pos + 32);
            const auto diskStart = get16(directory, pos + 34);
            const auto localOffset = get32(directory, pos + 42);
            const auto recordEnd = pos + zip::kCentralSize + nameLength + extraLength + commentLength;
            if (recordEnd > directory.size()) throw ProjectArchiveError("central directory is truncated");
            const auto name = directory.substr(pos + zip::kCentralSize, nameLength);

            if (method != 0) throw ProjectArchiveError("compressed entry '" + name + "': only stored entries are supported");
            if ((flags & (zip::kFlagEncrypted | zip::kFlagDataDescriptor)) != 0)
                throw ProjectArchiveError("entry '" + name + "' is encrypted or uses a data descriptor (not supported)");
            if (compressed != uncompressed) throw ProjectArchiveError("entry '" + name + "' has inconsistent sizes");
            if (diskStart != 0) throw ProjectArchiveError("multi-disk archives are not supported");
            if (!isSafeEntryName(name)) throw ProjectArchiveError("unsafe entry name in archive: " + name);
            if (!names.insert(name).second) throw ProjectArchiveError("duplicate entry: " + name);
            if (static_cast<std::uint64_t>(localOffset) + zip::kLocalSize + nameLength + compressed > directoryOffset)
                throw ProjectArchiveError("entry '" + name + "' lies outside the archive data");
            entries.push_back({name, flags, crc, uncompressed, localOffset});
            pos = recordEnd;
        }
        if (pos != directory.size()) throw ProjectArchiveError("trailing bytes in the central directory");
        if (names.count(kProjectArchiveManifestName) == 0)
            throw ProjectArchiveError("archive has no " + std::string(kProjectArchiveManifestName));

        const auto manifest = destination / kProjectArchiveManifestName;
        if (fs::exists(manifest)) throw ProjectArchiveError("destination already holds a project: " + destination.string());
        createdDestination = !fs::exists(destination);
        fs::create_directories(destination);

        for (const auto& entry : entries) {
            in.seekg(entry.offset);
            std::string local(zip::kLocalSize, '\0');
            in.read(&local[0], static_cast<std::streamsize>(local.size()));
            if (!in || get32(local, 0) != zip::kLocalSig) throw ProjectArchiveError("bad local header for " + entry.name);
            const auto localNameLength = get16(local, 26);
            const auto localExtraLength = get16(local, 28);
            if (localNameLength != entry.name.size() || get16(local, 6) != entry.flags ||
                get32(local, 14) != entry.crc || get32(local, 18) != entry.size ||
                get32(local, 22) != entry.size)
                throw ProjectArchiveError("local header of " + entry.name + " disagrees with the central directory");
            std::string localName(localNameLength, '\0');
            in.read(&localName[0], static_cast<std::streamsize>(localName.size()));
            if (localName != entry.name) throw ProjectArchiveError("local name of " + entry.name + " disagrees");
            in.seekg(localExtraLength, std::ios::cur);

            const auto target = destination / fs::path(entry.name);
            if (fs::exists(target)) throw ProjectArchiveError("refusing to overwrite " + target.string());
            fs::create_directories(target.parent_path());
            std::ofstream out(target, std::ios::binary);
            if (!out) throw ProjectArchiveError("cannot create " + target.string());
            written.push_back(target);

            std::string buffer(kChunk, '\0');
            std::uint64_t remaining = entry.size;
            std::uint32_t c = 0xFFFFFFFFu;
            while (remaining > 0) {
                const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
                in.read(&buffer[0], static_cast<std::streamsize>(want));
                if (static_cast<std::size_t>(in.gcount()) != want) throw ProjectArchiveError("truncated entry " + entry.name);
                c = crcUpdate(c, buffer.data(), want);
                out.write(buffer.data(), static_cast<std::streamsize>(want));
                remaining -= want;
            }
            out.close();
            if (!out) throw ProjectArchiveError("cannot write " + target.string());
            if ((c ^ 0xFFFFFFFFu) != entry.crc) throw ProjectArchiveError("CRC-32 mismatch in " + entry.name);
        }

        const auto project = OpenModelProject::load(manifest);
        const auto issues = project.verifyAssetIntegrity(manifest);
        if (!issues.empty())
            throw ProjectArchiveError("imported asset '" + issues.front().assetId + "' failed verification: " + issues.front().message);
        return manifest;
    } catch (const ProjectArchiveError&) {
        for (const auto& path : written) { std::error_code ignored; fs::remove(path, ignored); }
        if (createdDestination) { std::error_code ignored; fs::remove_all(destination, ignored); }
        throw;
    } catch (const std::exception& error) {
        for (const auto& path : written) { std::error_code ignored; fs::remove(path, ignored); }
        if (createdDestination) { std::error_code ignored; fs::remove_all(destination, ignored); }
        throw ProjectArchiveError(error.what());
    }
}

} // namespace OpenModel
