#include <OpenModel/MeshBinaryIO.hpp>

#include <algorithm>
#include <chrono>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <system_error>
#include <type_traits>
#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#endif

namespace {
constexpr std::array<char, 8> kMagic{{'M','G','M','B','\r','\n',0x1a,'\n'}};
constexpr std::uint16_t kMajor = 1;
constexpr std::uint16_t kMinor = 1;
constexpr std::uint32_t kEndianMarker = 0x01020304u;
constexpr std::uint64_t kHeaderSize = 64;
constexpr std::uint64_t kDirectoryRecordSize = 48;
constexpr std::uint32_t kLegacySectionCount = 5;
constexpr std::uint32_t kSectionCount = 6;

enum SectionId : std::uint32_t { NodeIds = 1, Coordinates = 2, Blocks = 3, NodeSets = 4, PhysicalGroups = 5, MeshGroups = 6 };
struct DirectoryEntry {
    std::uint32_t id = 0;
    std::uint32_t flags = 0;
    std::uint64_t offset = 0;
    std::uint64_t storedSize = 0;
    std::uint64_t rawSize = 0;
    std::uint64_t count = 0;
    std::uint32_t checksum = 0;
    std::uint32_t reserved = 0;
};

bool hostLittleEndian() noexcept {
    const std::uint16_t one = 1;
    return *reinterpret_cast<const unsigned char*>(&one) == 1;
}

template<class T>
void writeLE(std::ostream& out, T value) {
    static_assert(std::is_integral<T>::value, "integral required");
    using U = typename std::make_unsigned<T>::type;
    U v = static_cast<U>(value);
    unsigned char bytes[sizeof(T)];
    for (std::size_t i = 0; i < sizeof(T); ++i) bytes[i] = static_cast<unsigned char>((v >> (i * 8)) & U(0xff));
    out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
    if (!out) throw MeshBinaryIO::Error("MGMB: write failed");
}

template<class T>
T readLE(std::istream& in) {
    static_assert(std::is_integral<T>::value, "integral required");
    unsigned char bytes[sizeof(T)];
    in.read(reinterpret_cast<char*>(bytes), sizeof(bytes));
    if (!in) throw MeshBinaryIO::Error("MGMB: truncated file");
    using U = typename std::make_unsigned<T>::type;
    U v = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) v |= static_cast<U>(bytes[i]) << (i * 8);
    return static_cast<T>(v);
}

void writeU64Vector(std::ostream& out, const std::vector<std::uint64_t>& values) {
    if (hostLittleEndian() && !values.empty()) {
        if (values.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(std::uint64_t)) throw MeshBinaryIO::Error("MGMB: integer array exceeds stream limits");
        out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(std::uint64_t)));
        if (!out) throw MeshBinaryIO::Error("MGMB: write failed");
    } else for (auto v : values) writeLE(out, v);
}
void writeU32Indices(std::ostream& out, const std::vector<NodeIndex>& values) {
    constexpr std::size_t chunkItems = 8192;
    std::array<unsigned char, chunkItems * 4> buffer{};
    std::size_t offset = 0;
    while (offset < values.size()) {
        const auto count = std::min(chunkItems, values.size() - offset);
        for (std::size_t i = 0; i < count; ++i) {
            const auto value = static_cast<std::uint32_t>(values[offset + i]);
            buffer[i * 4] = static_cast<unsigned char>(value & 0xffu);
            buffer[i * 4 + 1] = static_cast<unsigned char>((value >> 8) & 0xffu);
            buffer[i * 4 + 2] = static_cast<unsigned char>((value >> 16) & 0xffu);
            buffer[i * 4 + 3] = static_cast<unsigned char>((value >> 24) & 0xffu);
        }
        out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(count * 4));
        if (!out) throw MeshBinaryIO::Error("MGMB: write failed");
        offset += count;
    }
}
void readU32Indices(std::istream& in, std::vector<NodeIndex>& values) {
    constexpr std::size_t chunkItems = 8192;
    std::array<unsigned char, chunkItems * 4> buffer{};
    std::size_t offset = 0;
    while (offset < values.size()) {
        const auto count = std::min(chunkItems, values.size() - offset);
        in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(count * 4));
        if (!in) throw MeshBinaryIO::Error("MGMB: truncated 32-bit index array");
        for (std::size_t i = 0; i < count; ++i) {
            const auto* p = buffer.data() + i * 4;
            values[offset + i] = static_cast<NodeIndex>(p[0]) |
                (static_cast<NodeIndex>(p[1]) << 8) |
                (static_cast<NodeIndex>(p[2]) << 16) |
                (static_cast<NodeIndex>(p[3]) << 24);
        }
        offset += count;
    }
}

void writeDoubleVector(std::ostream& out, const std::vector<double>& values) {
    static_assert(sizeof(double) == 8, "MGMB requires 64-bit double");
    static_assert(std::numeric_limits<double>::is_iec559, "MGMB requires IEEE-754 double");
    if (hostLittleEndian() && !values.empty()) {
        if (values.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(double)) throw MeshBinaryIO::Error("MGMB: coordinate array exceeds stream limits");
        out.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(double)));
        if (!out) throw MeshBinaryIO::Error("MGMB: write failed");
    } else {
        for (double value : values) {
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value), "double width mismatch");
            std::memcpy(&bits, &value, sizeof(bits));
            writeLE(out, bits);
        }
    }
}
void readU64Vector(std::istream& in, std::vector<std::uint64_t>& values) {
    if (hostLittleEndian() && !values.empty()) {
        if (values.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(std::uint64_t)) throw MeshBinaryIO::Error("MGMB: integer array exceeds stream limits");
        in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(std::uint64_t)));
        if (!in) throw MeshBinaryIO::Error("MGMB: truncated integer array");
    } else for (auto& v : values) v = readLE<std::uint64_t>(in);
}
void readDoubleVector(std::istream& in, std::vector<double>& values) {
    if (hostLittleEndian() && !values.empty()) {
        if (values.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()) / sizeof(double)) throw MeshBinaryIO::Error("MGMB: coordinate array exceeds stream limits");
        in.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(values.size() * sizeof(double)));
        if (!in) throw MeshBinaryIO::Error("MGMB: truncated coordinate array");
    } else {
        for (double& value : values) {
            const auto bits = readLE<std::uint64_t>(in);
            std::memcpy(&value, &bits, sizeof(value));
        }
    }
}

void writeString(std::ostream& out, const std::string& value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) throw MeshBinaryIO::Error("MGMB: string too long");
    writeLE(out, static_cast<std::uint32_t>(value.size()));
    if (!value.empty()) out.write(value.data(), static_cast<std::streamsize>(value.size()));
    if (!out) throw MeshBinaryIO::Error("MGMB: write failed");
}
std::uint32_t typeCode(ElementType type) {
    switch (type) {
    case ElementType::Unknown: return 0; case ElementType::Point1: return 1;
    case ElementType::Line2: return 2; case ElementType::Line3: return 3;
    case ElementType::Triangle3: return 4; case ElementType::Triangle6: return 5;
    case ElementType::Quadrilateral4: return 6; case ElementType::Quadrilateral8: return 7;
    case ElementType::Quadrilateral9: return 8; case ElementType::Tetrahedron4: return 9;
    case ElementType::Tetrahedron10: return 10; case ElementType::Hexahedron8: return 11;
    case ElementType::Hexahedron20: return 12; case ElementType::Hexahedron27: return 13;
    case ElementType::Prism6: return 14; case ElementType::Prism15: return 15;
    case ElementType::Pyramid5: return 16;
    }
    throw MeshBinaryIO::Error("MGMB: unsupported element type");
}
ElementType decodeType(std::uint32_t code) {
    switch (code) {
    case 0: return ElementType::Unknown; case 1: return ElementType::Point1;
    case 2: return ElementType::Line2; case 3: return ElementType::Line3;
    case 4: return ElementType::Triangle3; case 5: return ElementType::Triangle6;
    case 6: return ElementType::Quadrilateral4; case 7: return ElementType::Quadrilateral8;
    case 8: return ElementType::Quadrilateral9; case 9: return ElementType::Tetrahedron4;
    case 10: return ElementType::Tetrahedron10; case 11: return ElementType::Hexahedron8;
    case 12: return ElementType::Hexahedron20; case 13: return ElementType::Hexahedron27;
    case 14: return ElementType::Prism6; case 15: return ElementType::Prism15;
    case 16: return ElementType::Pyramid5;
    default: throw MeshBinaryIO::Error("MGMB: unknown element type code");
    }
}

void writeHeader(std::ostream& out, std::uint64_t nodes, std::uint64_t elements,
                 std::uint32_t blocks, std::uint64_t directoryOffset, std::uint64_t fileSize) {
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    writeLE(out, kMajor); writeLE(out, kMinor); writeLE(out, kEndianMarker);
    writeLE(out, std::uint64_t{0}); // feature flags; v1 requires zero
    writeLE(out, nodes); writeLE(out, elements); writeLE(out, blocks);
    writeLE(out, kSectionCount); writeLE(out, directoryOffset); writeLE(out, fileSize);
    if (!out) throw MeshBinaryIO::Error("MGMB: failed writing header");
}
void writeDirectoryEntry(std::ostream& out, const DirectoryEntry& e) {
    writeLE(out, e.id); writeLE(out, e.flags); writeLE(out, e.offset);
    writeLE(out, e.storedSize); writeLE(out, e.rawSize); writeLE(out, e.count);
    writeLE(out, e.checksum); writeLE(out, e.reserved);
}
DirectoryEntry readDirectoryEntry(std::istream& in) {
    DirectoryEntry e;
    e.id = readLE<std::uint32_t>(in); e.flags = readLE<std::uint32_t>(in);
    e.offset = readLE<std::uint64_t>(in); e.storedSize = readLE<std::uint64_t>(in);
    e.rawSize = readLE<std::uint64_t>(in); e.count = readLE<std::uint64_t>(in);
    e.checksum = readLE<std::uint32_t>(in); e.reserved = readLE<std::uint32_t>(in);
    return e;
}

std::uint64_t tell(std::ostream& out) {
    const auto p = out.tellp();
    if (p < 0) throw MeshBinaryIO::Error("MGMB: cannot determine output offset");
    return static_cast<std::uint64_t>(p);
}
std::uint64_t fileLength(std::ifstream& in) {
    in.seekg(0, std::ios::end);
    const auto p = in.tellg();
    if (p < 0) throw MeshBinaryIO::Error("MGMB: cannot determine file length");
    return static_cast<std::uint64_t>(p);
}
void ensureSection(const DirectoryEntry& e, std::uint64_t fileSize, std::uint64_t directoryOffset) {
    if (e.flags != 0 || e.checksum != 0 || e.reserved != 0 || e.storedSize != e.rawSize)
        throw MeshBinaryIO::Error("MGMB: unsupported section flags, compression, or checksum");
    if (e.offset < kHeaderSize || e.offset > directoryOffset || e.storedSize > directoryOffset - e.offset ||
        e.offset > fileSize || e.storedSize > fileSize - e.offset)
        throw MeshBinaryIO::Error("MGMB: section is outside the file or overlaps the directory");
}
void requireConsumed(std::istream& in, const DirectoryEntry& e) {
    const auto p = in.tellg();
    if (p < 0 || static_cast<std::uint64_t>(p) != e.offset + e.rawSize)
        throw MeshBinaryIO::Error("MGMB: section size does not match its contents");
}

// Immutable metadata obtained before any section payload is decoded. Keeping this
// phase independent makes it possible to schedule section reads concurrently later.
struct FileLayout {
    std::uint64_t nodeCount = 0;
    std::uint64_t elementCount = 0;
    std::uint32_t blockCount = 0;
    std::uint16_t minorVersion = 0;
    std::map<std::uint32_t, DirectoryEntry> sections;
};

FileLayout parseFileLayout(std::istream& in, std::uint64_t actualSize) {
    if (actualSize < kHeaderSize) throw MeshBinaryIO::Error("MGMB: file is shorter than the header");
    in.clear();
    in.seekg(0, std::ios::beg);
    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) throw MeshBinaryIO::Error("MGMB: invalid magic signature");

    const auto major = readLE<std::uint16_t>(in);
    const auto minor = readLE<std::uint16_t>(in);
    const auto endian = readLE<std::uint32_t>(in);
    const auto flags = readLE<std::uint64_t>(in);
    FileLayout layout;
    layout.minorVersion = minor;
    layout.nodeCount = readLE<std::uint64_t>(in);
    layout.elementCount = readLE<std::uint64_t>(in);
    layout.blockCount = readLE<std::uint32_t>(in);
    const auto sectionCount = readLE<std::uint32_t>(in);
    const auto directoryOffset = readLE<std::uint64_t>(in);
    const auto declaredFileSize = readLE<std::uint64_t>(in);

    if (major != kMajor || minor > kMinor) throw MeshBinaryIO::Error("MGMB: unsupported format version");
    if (endian != kEndianMarker || flags != 0) throw MeshBinaryIO::Error("MGMB: unsupported endian marker or feature flags");
    const auto expectedSectionCount = minor == 0 ? kLegacySectionCount : kSectionCount;
    if (sectionCount != expectedSectionCount) throw MeshBinaryIO::Error("MGMB: invalid section count for format version");
    if (declaredFileSize != actualSize) throw MeshBinaryIO::Error("MGMB: declared file size does not match actual size");
    if (directoryOffset < kHeaderSize || directoryOffset > actualSize ||
        static_cast<std::uint64_t>(sectionCount) * kDirectoryRecordSize > actualSize - directoryOffset ||
        directoryOffset + static_cast<std::uint64_t>(sectionCount) * kDirectoryRecordSize != actualSize)
        throw MeshBinaryIO::Error("MGMB: invalid directory offset or size");
    if (layout.nodeCount > std::numeric_limits<std::size_t>::max() / 3 ||
        layout.nodeCount > std::numeric_limits<std::uint64_t>::max() / 24 ||
        layout.blockCount > std::numeric_limits<std::size_t>::max())
        throw MeshBinaryIO::Error("MGMB: mesh counts exceed addressable memory");

    in.clear();
    in.seekg(static_cast<std::streamoff>(directoryOffset), std::ios::beg);
    if (!in) throw MeshBinaryIO::Error("MGMB: cannot seek to section directory");
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    ranges.reserve(sectionCount);
    for (std::uint32_t i = 0; i < sectionCount; ++i) {
        auto entry = readDirectoryEntry(in);
        if (entry.id < NodeIds || entry.id > (minor == 0 ? PhysicalGroups : MeshGroups) || !layout.sections.emplace(entry.id, entry).second)
            throw MeshBinaryIO::Error("MGMB: unknown or duplicate section ID");
        ensureSection(entry, actualSize, directoryOffset);
        ranges.emplace_back(entry.offset, entry.offset + entry.storedSize);
    }
    if (layout.sections.size() != expectedSectionCount) throw MeshBinaryIO::Error("MGMB: missing required section");
    std::sort(ranges.begin(), ranges.end());
    for (std::size_t i = 1; i < ranges.size(); ++i)
        if (ranges[i].first < ranges[i - 1].second) throw MeshBinaryIO::Error("MGMB: overlapping sections");
    return layout;
}
}

void MeshBinaryIO::write(const std::filesystem::path& path, const MeshData& mesh, const Options& options) {
    if (options.validateMesh) {
        const auto errors = mesh.validate();
        if (!errors.empty()) throw Error("MGMB: refusing to write invalid mesh: " + errors.front());
    }
    if (path.empty()) throw Error("MGMB: output path is empty");
    if (mesh.m_blocks.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.m_nodeSets.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.m_physicalGroups.size() > std::numeric_limits<std::uint32_t>::max() ||
        mesh.m_groupRegistry.groups().size() > std::numeric_limits<std::uint32_t>::max())
        throw Error("MGMB: section count exceeds format limits");
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto temporary = std::filesystem::path(path.string() + ".tmp." + std::to_string(nonce));
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw Error("MGMB: cannot open output file");
        writeHeader(out, mesh.m_nodeIds.size(), mesh.getNumElements(), static_cast<std::uint32_t>(mesh.m_blocks.size()), 0, 0);
        std::vector<DirectoryEntry> directory;
        directory.reserve(kSectionCount);
        auto beginSection = [&](std::uint32_t id, std::uint64_t count) {
            DirectoryEntry e; e.id = id; e.offset = tell(out); e.count = count; return e;
        };
        auto endSection = [&](DirectoryEntry& e) { e.storedSize = e.rawSize = tell(out) - e.offset; directory.push_back(e); };

        auto e = beginSection(NodeIds, mesh.m_nodeIds.size());
        writeU64Vector(out, mesh.m_nodeIds); endSection(e);
        e = beginSection(Coordinates, mesh.m_nodeIds.size());
        writeDoubleVector(out, mesh.m_coordinates); endSection(e);

        e = beginSection(Blocks, mesh.m_blocks.size());
        writeLE(out, static_cast<std::uint32_t>(mesh.m_blocks.size()));
        for (const auto& block : mesh.m_blocks) {
            writeString(out, block.name); writeLE(out, typeCode(block.type));
            if (block.nodesPerElement > std::numeric_limits<std::uint32_t>::max()) throw Error("MGMB: nodes-per-element exceeds format limit");
            writeLE(out, static_cast<std::uint32_t>(block.nodesPerElement));
            writeLE(out, static_cast<std::uint64_t>(block.ids.size()));
            const bool narrow = mesh.m_nodeIds.size() <= (std::uint64_t{1} << 32) &&
                std::all_of(block.connectivity.begin(), block.connectivity.end(), [](NodeIndex i) { return i <= std::numeric_limits<std::uint32_t>::max(); });
            writeLE(out, static_cast<std::uint8_t>(narrow ? 4 : 8));
            for (int i = 0; i < 7; ++i) writeLE(out, std::uint8_t{0});
            writeU64Vector(out, block.ids);
            if (narrow) {
                writeU32Indices(out, block.connectivity);
            } else writeU64Vector(out, block.connectivity);
        }
        endSection(e);

        e = beginSection(NodeSets, mesh.m_nodeSets.size());
        std::vector<std::string> setNames; setNames.reserve(mesh.m_nodeSets.size());
        for (const auto& pair : mesh.m_nodeSets) setNames.push_back(pair.first);
        std::sort(setNames.begin(), setNames.end());
        writeLE(out, static_cast<std::uint32_t>(setNames.size()));
        for (const auto& name : setNames) {
            const auto& indices = mesh.m_nodeSets.at(name);
            writeString(out, name); writeLE(out, static_cast<std::uint64_t>(indices.size()));
            const bool narrow = mesh.m_nodeIds.size() <= (std::uint64_t{1} << 32);
            writeLE(out, static_cast<std::uint8_t>(narrow ? 4 : 8));
            for (int i = 0; i < 7; ++i) writeLE(out, std::uint8_t{0});
            if (narrow) writeU32Indices(out, indices);
            else writeU64Vector(out, indices);
        }
        endSection(e);

        e = beginSection(PhysicalGroups, mesh.m_physicalGroups.size());
        writeLE(out, static_cast<std::uint32_t>(mesh.m_physicalGroups.size()));
        for (const auto& group : mesh.m_physicalGroups) {
            writeString(out, group.name); writeLE(out, static_cast<std::int32_t>(group.dimension));
            writeLE(out, static_cast<std::int32_t>(group.tag)); writeString(out, group.materialName);
            writeLE(out, static_cast<std::uint64_t>(group.elementIds.size()));
            writeU64Vector(out, group.elementIds);
        }
        endSection(e);

        // MGMB 1.1: backend-independent mesh groups. Keep v1.0 readable;
        // all variable-length records are length-prefixed and little-endian.
        e = beginSection(MeshGroups, mesh.m_groupRegistry.groups().size());
        writeLE(out, static_cast<std::uint32_t>(mesh.m_groupRegistry.groups().size()));
        for (const auto& group : mesh.m_groupRegistry.groups()) {
            writeString(out, group.name);
            writeLE(out, static_cast<std::int32_t>(group.dimension));
            writeLE(out, static_cast<std::uint8_t>(group.source));
            for (int r = 0; r < 3; ++r) writeLE(out, std::uint8_t{0});
            writeLE(out, static_cast<std::uint64_t>(group.sourceEntityIds.size()));
            for (const auto id : group.sourceEntityIds) writeLE(out, static_cast<std::int32_t>(id));
            writeLE(out, static_cast<std::uint64_t>(group.elementIds.size()));
            writeU64Vector(out, group.elementIds);
            writeLE(out, static_cast<std::uint64_t>(group.nodeIndices.size()));
            writeU64Vector(out, group.nodeIndices);
        }
        endSection(e);

        const auto directoryOffset = tell(out);
        for (const auto& entry : directory) writeDirectoryEntry(out, entry);
        const auto finalSize = tell(out);
        out.seekp(0, std::ios::beg);
        writeHeader(out, mesh.m_nodeIds.size(), mesh.getNumElements(), static_cast<std::uint32_t>(mesh.m_blocks.size()), directoryOffset, finalSize);
        out.flush();
        if (!out) throw Error("MGMB: failed flushing output file");
        out.close();
        if (!out) throw Error("MGMB: failed closing output file");
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw Error("MGMB: cannot publish output file (Windows error " + std::to_string(GetLastError()) + ")");
#else
        std::error_code ec;
        std::filesystem::rename(temporary, path, ec);
        if (ec) throw Error("MGMB: cannot publish output file: " + ec.message());
#endif
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        throw;
    }
}

MeshData MeshBinaryIO::read(const std::filesystem::path& path, const Options& options) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw Error("MGMB: cannot open input file");
    const auto actualSize = fileLength(in);

    // Phase 1: parse and validate the immutable file layout (header + directory).
    // Phase 2 below reads and decodes each payload using the validated offsets.
    const auto layout = parseFileLayout(in, actualSize);
    const auto nodeCount = layout.nodeCount;
    const auto elementCount = layout.elementCount;
    const auto blockCount = layout.blockCount;
    const auto& sections = layout.sections;

    MeshData mesh;
    auto openSection = [&](std::uint32_t id) -> const DirectoryEntry& {
        const auto& entry = sections.at(id);
        in.clear(); in.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
        if (!in) throw Error("MGMB: cannot seek to section");
        return entry;
    };
    {
        const auto& s = openSection(NodeIds);
        if (s.count != nodeCount || s.rawSize != nodeCount * sizeof(std::uint64_t)) throw Error("MGMB: node ID section size mismatch");
        mesh.m_nodeIds.resize(static_cast<std::size_t>(nodeCount));
        readU64Vector(in, mesh.m_nodeIds); requireConsumed(in, s);
        mesh.m_nodeIdToIndex.reserve(mesh.m_nodeIds.size());
        for (std::size_t i = 0; i < mesh.m_nodeIds.size(); ++i)
            if (!mesh.m_nodeIdToIndex.emplace(mesh.m_nodeIds[i], static_cast<NodeIndex>(i)).second) throw Error("MGMB: duplicate node ID");
    }
    {
        const auto& s = openSection(Coordinates);
        if (s.count != nodeCount || nodeCount > std::numeric_limits<std::uint64_t>::max() / 24 || s.rawSize != nodeCount * 24)
            throw Error("MGMB: coordinate section size mismatch");
        mesh.m_coordinates.resize(static_cast<std::size_t>(nodeCount) * 3);
        readDoubleVector(in, mesh.m_coordinates); requireConsumed(in, s);
    }
    {
        const auto& s = openSection(Blocks);
        const auto count = readLE<std::uint32_t>(in);
        if (count != blockCount || s.count != blockCount) throw Error("MGMB: block count mismatch");
        if (s.rawSize < 4 || count > (s.rawSize - 4) / 28) throw Error("MGMB: block count exceeds section bounds");
        mesh.m_blocks.reserve(count); mesh.m_blockLookup.reserve(count);
        std::uint64_t totalElements = 0;
        for (std::uint32_t b = 0; b < count; ++b) {
            const auto nameLength = readLE<std::uint32_t>(in);
            const auto namePosition = static_cast<std::uint64_t>(in.tellg());
            if (namePosition > s.offset + s.rawSize || nameLength > s.offset + s.rawSize - namePosition)
                throw Error("MGMB: invalid block name length");
            std::string name(nameLength, '\0');
            if (nameLength) in.read(&name[0], static_cast<std::streamsize>(nameLength));
            if (!in) throw Error("MGMB: truncated block name");
            const auto type = decodeType(readLE<std::uint32_t>(in));
            const auto nodesPerElement = readLE<std::uint32_t>(in);
            const auto nElements = readLE<std::uint64_t>(in);
            const auto width = readLE<std::uint8_t>(in);
            for (int i = 0; i < 7; ++i) if (readLE<std::uint8_t>(in) != 0) throw Error("MGMB: nonzero reserved block byte");
            if ((width != 4 && width != 8) || nodesPerElement == 0 || nElements > std::numeric_limits<std::size_t>::max() ||
                nElements > std::numeric_limits<std::uint64_t>::max() / nodesPerElement)
                throw Error("MGMB: invalid block metadata");
            if (elementNodeCount(type) != 0 && elementNodeCount(type) != nodesPerElement) throw Error("MGMB: element topology/node count mismatch");
            if (nElements > std::numeric_limits<std::uint64_t>::max() - totalElements) throw Error("MGMB: element count overflow");
            totalElements += nElements;
            ElementBlock block; block.name = std::move(name); block.type = type; block.nodesPerElement = nodesPerElement;
            const auto connectivityCount = nElements * nodesPerElement;
            if (connectivityCount > std::numeric_limits<std::size_t>::max()) throw Error("MGMB: connectivity exceeds addressable memory");
            const auto position = static_cast<std::uint64_t>(in.tellg());
            const auto sectionEnd = s.offset + s.rawSize;
            if (position > sectionEnd || nElements > (sectionEnd - position) / sizeof(ElementId)) throw Error("MGMB: block IDs exceed section bounds");
            const auto afterIds = position + nElements * sizeof(ElementId);
            if (afterIds > sectionEnd || connectivityCount > (sectionEnd - afterIds) / width) throw Error("MGMB: block connectivity exceeds section bounds");
            block.ids.resize(static_cast<std::size_t>(nElements)); readU64Vector(in, block.ids);
            block.connectivity.resize(static_cast<std::size_t>(connectivityCount));
            if (width == 4) readU32Indices(in, block.connectivity);
            else readU64Vector(in, block.connectivity);
            for (const auto index : block.connectivity) if (index >= nodeCount) throw Error("MGMB: connectivity index out of range");
            if (block.name.empty() || !mesh.m_blockLookup.emplace(block.name, mesh.m_blocks.size()).second) throw Error("MGMB: empty or duplicate block name");
            mesh.m_blocks.push_back(std::move(block));
        }
        if (totalElements != elementCount) throw Error("MGMB: total element count mismatch");
        requireConsumed(in, s);
    }
    {
        const auto& s = openSection(NodeSets);
        const auto count = readLE<std::uint32_t>(in);
        if (count != s.count) throw Error("MGMB: node set count mismatch");
        if (s.rawSize < 4 || count > (s.rawSize - 4) / 20) throw Error("MGMB: node set count exceeds section bounds");
        for (std::uint32_t i = 0; i < count; ++i) {
            // Parse strings against the section end before allocating their payload.
            auto nameLength = readLE<std::uint32_t>(in);
            const auto current = static_cast<std::uint64_t>(in.tellg());
            if (current > s.offset + s.rawSize || nameLength > s.offset + s.rawSize - current) throw Error("MGMB: invalid node set name length");
            std::string name(nameLength, '\0'); if (nameLength) in.read(&name[0], nameLength);
            const auto n = readLE<std::uint64_t>(in);
            const auto width = readLE<std::uint8_t>(in);
            for (int r = 0; r < 7; ++r) if (readLE<std::uint8_t>(in) != 0) throw Error("MGMB: nonzero reserved node set byte");
            if (name.empty() || (width != 4 && width != 8) || n > std::numeric_limits<std::size_t>::max()) throw Error("MGMB: invalid node set metadata");
            if (n > (s.offset + s.rawSize - static_cast<std::uint64_t>(in.tellg())) / width) throw Error("MGMB: node set data exceeds section");
            std::vector<NodeIndex> indices(static_cast<std::size_t>(n));
            if (width == 4) readU32Indices(in, indices); else readU64Vector(in, indices);
            for (auto index : indices) if (index >= nodeCount) throw Error("MGMB: node set index out of range");
            if (!mesh.m_nodeSets.emplace(std::move(name), std::move(indices)).second) throw Error("MGMB: duplicate node set name");
        }
        requireConsumed(in, s);
    }
    {
        const auto& s = openSection(PhysicalGroups);
        const auto count = readLE<std::uint32_t>(in);
        if (count != s.count) throw Error("MGMB: physical group count mismatch");
        if (s.rawSize < 4 || count > (s.rawSize - 4) / 24) throw Error("MGMB: physical group count exceeds section bounds");
        for (std::uint32_t i = 0; i < count; ++i) {
            auto readBoundedString = [&]() {
                const auto length = readLE<std::uint32_t>(in);
                const auto current = static_cast<std::uint64_t>(in.tellg());
                if (current > s.offset + s.rawSize || length > s.offset + s.rawSize - current) throw Error("MGMB: invalid physical group string length");
                std::string value(length, '\0'); if (length) in.read(&value[0], length);
                if (!in) throw Error("MGMB: truncated physical group string");
                return value;
            };
            MeshPhysicalGroup group;
            group.name = readBoundedString(); group.dimension = readLE<std::int32_t>(in); group.tag = readLE<std::int32_t>(in);
            group.materialName = readBoundedString();
            const auto n = readLE<std::uint64_t>(in);
            if (n > std::numeric_limits<std::size_t>::max() || n > (s.offset + s.rawSize - static_cast<std::uint64_t>(in.tellg())) / sizeof(ElementId))
                throw Error("MGMB: physical group element list exceeds section");
            group.elementIds.resize(static_cast<std::size_t>(n)); readU64Vector(in, group.elementIds);
            mesh.m_physicalGroups.push_back(std::move(group));
        }
        requireConsumed(in, s);
    }
    // MGMB 1.0 files have no MeshGroups section. Treat that as an empty
    // registry; files written by this version always carry section 6.
    if (layout.minorVersion >= 1) {
        const auto& s = openSection(MeshGroups);
        const auto count = readLE<std::uint32_t>(in);
        if (count != s.count || s.rawSize < 4 || count > (s.rawSize - 4) / 28)
            throw Error("MGMB: mesh group count mismatch or exceeds section bounds");
        for (std::uint32_t i = 0; i < count; ++i) {
            auto readBoundedString = [&]() {
                const auto length = readLE<std::uint32_t>(in);
                const auto current = static_cast<std::uint64_t>(in.tellg());
                if (current > s.offset + s.rawSize || length > s.offset + s.rawSize - current)
                    throw Error("MGMB: invalid mesh group name length");
                std::string value(length, '\\0');
                if (length) in.read(&value[0], static_cast<std::streamsize>(length));
                if (!in) throw Error("MGMB: truncated mesh group name");
                return value;
            };
            MeshGroup group;
            group.name = readBoundedString();
            group.dimension = readLE<std::int32_t>(in);
            const auto source = readLE<std::uint8_t>(in);
            for (int r = 0; r < 3; ++r)
                if (readLE<std::uint8_t>(in) != 0) throw Error("MGMB: nonzero reserved mesh group byte");
            if (source > static_cast<std::uint8_t>(MeshGroupSource::Imported))
                throw Error("MGMB: invalid mesh group source");
            group.source = static_cast<MeshGroupSource>(source);
            auto readCount = [&](const char* what) {
                const auto n = readLE<std::uint64_t>(in);
                const auto current = static_cast<std::uint64_t>(in.tellg());
                if (current > s.offset + s.rawSize || n > std::numeric_limits<std::size_t>::max())
                    throw Error(std::string("MGMB: invalid ") + what + " count");
                return n;
            };
            const auto sourceCount = readCount("mesh group source entity");
            if (sourceCount > (s.offset + s.rawSize - static_cast<std::uint64_t>(in.tellg())) / sizeof(std::int32_t))
                throw Error("MGMB: mesh group source entity list exceeds section");
            group.sourceEntityIds.resize(static_cast<std::size_t>(sourceCount));
            for (auto& id : group.sourceEntityIds) id = readLE<std::int32_t>(in);
            const auto elementCountInGroup = readCount("mesh group element");
            if (elementCountInGroup > (s.offset + s.rawSize - static_cast<std::uint64_t>(in.tellg())) / sizeof(ElementId))
                throw Error("MGMB: mesh group element list exceeds section");
            group.elementIds.resize(static_cast<std::size_t>(elementCountInGroup));
            readU64Vector(in, group.elementIds);
            const auto nodeCountInGroup = readCount("mesh group node");
            if (nodeCountInGroup > (s.offset + s.rawSize - static_cast<std::uint64_t>(in.tellg())) / sizeof(NodeIndex))
                throw Error("MGMB: mesh group node list exceeds section");
            group.nodeIndices.resize(static_cast<std::size_t>(nodeCountInGroup));
            readU64Vector(in, group.nodeIndices);
            try { mesh.addMeshGroup(std::move(group)); }
            catch (const std::exception& ex) { throw Error(std::string("MGMB: invalid mesh group: ") + ex.what()); }
        }
        requireConsumed(in, s);
    }
    if (options.validateMesh) {
        const auto errors = mesh.validate();
        if (!errors.empty()) throw Error("MGMB: loaded mesh is structurally invalid: " + errors.front());
    }
    return mesh;
}
