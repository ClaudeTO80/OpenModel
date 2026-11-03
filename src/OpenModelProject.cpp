#include <OpenModel/OpenModelProject.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <chrono>
#include <random>
#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#else
#  include <cstdio>
#endif

namespace OpenModel {
namespace {
const char* storageName(ProjectAssetStorage s) {
    return s == ProjectAssetStorage::Managed ? "managed" : "linked";
}
ProjectAssetStorage parseStorage(const std::string& s) {
    if (s == "managed") return ProjectAssetStorage::Managed;
    if (s == "linked") return ProjectAssetStorage::Linked;
    throw std::runtime_error("OpenModelProject: invalid asset storage mode");
}
bool knownRole(const std::string& role) {
    static const char* roles[] = {"cad.source", "cad.face_groups", "mesh.surface",
        "mesh.volume", "analysis.model", "results.solver", "results.metadata"};
    return std::find_if(std::begin(roles), std::end(roles),
        [&](const char* r) { return role == r; }) != std::end(roles);
}
bool pathEscapesRoot(const std::filesystem::path& relative) {
    if (relative.empty() || relative.is_absolute()) return true;
    int depth = 0;
    for (const auto& part : relative) {
        if (part == ".") continue;
        if (part == "..") { if (--depth < 0) return true; }
        else ++depth;
    }
    return false;
}
std::string sha256File(const std::filesystem::path& path) {
    static constexpr std::uint32_t k[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("OpenModelProject: cannot read asset: " + path.string());
    std::uint32_t h[8] = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                         0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    const auto rotr=[](std::uint32_t x,unsigned n){ return (x>>n)|(x<<(32u-n)); };
    const auto transform=[&](const unsigned char* block) {
        std::uint32_t w[64]{};
        for (unsigned i=0;i<16;++i) {
            const auto p=i*4u;
            w[i]=(std::uint32_t(block[p])<<24)|(std::uint32_t(block[p+1])<<16)|
                 (std::uint32_t(block[p+2])<<8)|std::uint32_t(block[p+3]);
        }
        for (unsigned i=16;i<64;++i) {
            const auto s0=rotr(w[i-15],7)^rotr(w[i-15],18)^(w[i-15]>>3);
            const auto s1=rotr(w[i-2],17)^rotr(w[i-2],19)^(w[i-2]>>10);
            w[i]=w[i-16]+s0+w[i-7]+s1;
        }
        std::uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4],f=h[5],g=h[6],z=h[7];
        for (unsigned i=0;i<64;++i) {
            const auto s1=rotr(e,6)^rotr(e,11)^rotr(e,25);
            const auto ch=(e&f)^(~e&g);
            const auto t1=z+s1+ch+k[i]+w[i];
            const auto s0=rotr(a,2)^rotr(a,13)^rotr(a,22);
            const auto maj=(a&b)^(a&c)^(b&c);
            const auto t2=s0+maj;
            z=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;h[5]+=f;h[6]+=g;h[7]+=z;
    };
    std::array<unsigned char,64> block{};
    std::uint64_t totalBytes=0;
    while (in.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()))) {
        transform(block.data());
        totalBytes += block.size();
    }
    const auto remaining=static_cast<std::size_t>(in.gcount());
    totalBytes += remaining;
    std::array<unsigned char,64> tail{};
    if (remaining) std::copy_n(block.data(), remaining, tail.data());
    tail[remaining]=0x80u;
    if (remaining>=56u) {
        transform(tail.data());
        tail.fill(0u);
    }
    const std::uint64_t bitLength=totalBytes*8u;
    for (unsigned i=0;i<8;++i)
        tail[63u-i]=static_cast<unsigned char>(bitLength>>(i*8u));
    transform(tail.data());
    if (!in.eof() && in.fail()) throw std::runtime_error("OpenModelProject: asset read failed: " + path.string());
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto v:h) out << std::setw(8) << v;
    return out.str();
}
std::filesystem::path tempPath(const std::filesystem::path& p) {
    std::random_device rd;
    std::ostringstream suffix;
    suffix << ".tmp." << std::hex << rd() << rd();
    return p.parent_path() / (p.filename().string() + suffix.str());
}
void replaceFile(const std::filesystem::path& from, const std::filesystem::path& to) {
#if defined(_WIN32)
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("OpenModelProject: atomic manifest replacement failed");
#else
    if (std::rename(from.c_str(), to.c_str()) != 0)
        throw std::runtime_error("OpenModelProject: atomic manifest replacement failed");
#endif
}
}

void OpenModelProject::addAsset(ProjectAsset asset) {
    if (asset.id.empty()) throw std::invalid_argument("OpenModelProject: asset ID cannot be empty");
    if (findAsset(asset.id)) throw std::invalid_argument("OpenModelProject: duplicate asset ID: " + asset.id);
    assets.push_back(std::move(asset));
}
bool OpenModelProject::removeAsset(const std::string& id) {
    const auto it = std::find_if(assets.begin(), assets.end(), [&](const auto& a) { return a.id == id; });
    if (it == assets.end()) return false;
    assets.erase(it);
    return true;
}
const ProjectAsset* OpenModelProject::findAsset(const std::string& id) const noexcept {
    const auto it = std::find_if(assets.begin(), assets.end(), [&](const auto& a) { return a.id == id; });
    return it == assets.end() ? nullptr : &*it;
}
ProjectAsset* OpenModelProject::findAsset(const std::string& id) noexcept {
    const auto it = std::find_if(assets.begin(), assets.end(), [&](const auto& a) { return a.id == id; });
    return it == assets.end() ? nullptr : &*it;
}
std::filesystem::path OpenModelProject::resolveAssetPath(const std::filesystem::path& manifest,
                                                          const ProjectAsset& asset) const {
    if (asset.storage == ProjectAssetStorage::Linked)
        return asset.path.is_absolute() ? asset.path : std::filesystem::absolute(manifest).parent_path() / asset.path;
    if (pathEscapesRoot(asset.path))
        throw std::runtime_error("OpenModelProject: managed asset path escapes project root: " + asset.id);
    return std::filesystem::absolute(manifest).parent_path() / asset.path;
}
std::vector<ProjectValidationIssue> OpenModelProject::validate(const std::filesystem::path& manifest) const {
    std::vector<ProjectValidationIssue> issues;
    if (metadata.id.empty()) issues.push_back({"", "project ID is empty"});
    if (metadata.name.empty()) issues.push_back({"", "project name is empty"});
    std::set<std::string> ids;
    for (const auto& asset : assets) {
        if (asset.id.empty()) issues.push_back({"", "asset ID is empty"});
        else if (!ids.insert(asset.id).second) issues.push_back({asset.id, "duplicate asset ID"});
        if (!knownRole(asset.role)) issues.push_back({asset.id, "unsupported asset role: " + asset.role});
        if (asset.path.empty()) issues.push_back({asset.id, "asset path is empty"});
        if (asset.storage == ProjectAssetStorage::Managed && pathEscapesRoot(asset.path))
            issues.push_back({asset.id, "managed asset path must remain inside project root"});
        if (!asset.sha256.empty() && (asset.sha256.size() != 64 ||
            !std::all_of(asset.sha256.begin(), asset.sha256.end(), [](unsigned char c) { return std::isxdigit(c) != 0; })))
            issues.push_back({asset.id, "SHA-256 must contain 64 hexadecimal characters"});
        (void)manifest;
    }
    return issues;
}
void OpenModelProject::refreshAssetIntegrity(const std::filesystem::path& manifest,
                                                    const std::string& assetId) {
    auto* asset = findAsset(assetId);
    if (!asset) throw std::invalid_argument("OpenModelProject: unknown asset ID: " + assetId);
    const auto path = resolveAssetPath(manifest, *asset);
    if (!std::filesystem::is_regular_file(path))
        throw std::runtime_error("OpenModelProject: asset is not a regular file: " + path.string());
    asset->sizeBytes = std::filesystem::file_size(path);
    asset->sha256 = sha256File(path);
}

std::vector<ProjectValidationIssue> OpenModelProject::verifyAssetIntegrity(
    const std::filesystem::path& manifest) const {
    std::vector<ProjectValidationIssue> issues;
    for (const auto& asset : assets) {
        if (asset.sha256.empty() && asset.sizeBytes == 0) continue;
        try {
            const auto path = resolveAssetPath(manifest, asset);
            if (!std::filesystem::is_regular_file(path)) {
                issues.push_back({asset.id, "asset file is missing: " + path.string()});
                continue;
            }
            const auto actualSize = std::filesystem::file_size(path);
            if (asset.sizeBytes != 0 && actualSize != asset.sizeBytes)
                issues.push_back({asset.id, "asset size mismatch"});
            if (!asset.sha256.empty() && sha256File(path) != asset.sha256)
                issues.push_back({asset.id, "asset SHA-256 mismatch"});
        } catch (const std::exception& e) {
            issues.push_back({asset.id, e.what()});
        }
    }
    return issues;
}

void OpenModelProject::save(const std::filesystem::path& manifest) const {
    const auto issues = validate(manifest);
    if (!issues.empty()) throw std::runtime_error("OpenModelProject: validation failed: " + issues.front().message);
    const auto parent = manifest.parent_path().empty() ? std::filesystem::current_path() : manifest.parent_path();
    std::filesystem::create_directories(parent);
    const auto temporary = tempPath(manifest);
    try {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("OpenModelProject: cannot create temporary manifest");
        out << "OPENMODEL_PROJECT " << schemaMajor << ' ' << schemaMinor << '\n';
        out << "PROJECT " << std::quoted(metadata.id) << ' ' << std::quoted(metadata.name) << ' '
            << std::quoted(metadata.description) << ' ' << std::quoted(metadata.applicationVersion) << ' '
            << std::quoted(metadata.createdUtc) << ' ' << std::quoted(metadata.modifiedUtc) << '\n';
        for (const auto& a : assets) {
            out << "ASSET " << std::quoted(a.id) << ' ' << std::quoted(a.role) << ' '
                << std::quoted(a.path.generic_string()) << ' ' << storageName(a.storage) << ' '
                << std::quoted(a.mediaType) << ' ' << a.sizeBytes << ' ' << std::quoted(a.sha256) << '\n';
        }
        out.flush();
        if (!out) throw std::runtime_error("OpenModelProject: manifest write failed");
        out.close();
        if (!out) throw std::runtime_error("OpenModelProject: manifest close failed");
        replaceFile(temporary, manifest);
    } catch (...) {
        std::error_code ec;
        std::filesystem::remove(temporary, ec);
        throw;
    }
}
OpenModelProject OpenModelProject::load(const std::filesystem::path& manifest) {
    std::ifstream in(manifest, std::ios::binary);
    if (!in) throw std::runtime_error("OpenModelProject: cannot open manifest: " + manifest.string());
    OpenModelProject project;
    std::string tag;
    unsigned major = 0, minor = 0;
    if (!(in >> tag >> major >> minor) || tag != "OPENMODEL_PROJECT")
        throw std::runtime_error("OpenModelProject: invalid manifest signature");
    if (major != schemaMajor || minor > schemaMinor)
        throw std::runtime_error("OpenModelProject: unsupported manifest schema version");
    bool hasProject = false;
    while (in >> tag) {
        if (tag == "PROJECT") {
            if (hasProject) throw std::runtime_error("OpenModelProject: duplicate PROJECT record");
            if (!(in >> std::quoted(project.metadata.id) >> std::quoted(project.metadata.name)
                  >> std::quoted(project.metadata.description) >> std::quoted(project.metadata.applicationVersion)
                  >> std::quoted(project.metadata.createdUtc) >> std::quoted(project.metadata.modifiedUtc)))
                throw std::runtime_error("OpenModelProject: malformed PROJECT record");
            hasProject = true;
        } else if (tag == "ASSET") {
            ProjectAsset a;
            std::string path, storage;
            if (!(in >> std::quoted(a.id) >> std::quoted(a.role) >> std::quoted(path) >> storage
                  >> std::quoted(a.mediaType) >> a.sizeBytes >> std::quoted(a.sha256)))
                throw std::runtime_error("OpenModelProject: malformed ASSET record");
            a.path = std::filesystem::u8path(path);
            a.storage = parseStorage(storage);
            project.addAsset(std::move(a));
        } else throw std::runtime_error("OpenModelProject: unknown manifest record: " + tag);
    }
    if (!in.eof()) throw std::runtime_error("OpenModelProject: manifest read error");
    if (!hasProject) throw std::runtime_error("OpenModelProject: missing PROJECT record");
    const auto issues = project.validate(manifest);
    if (!issues.empty()) throw std::runtime_error("OpenModelProject: invalid manifest: " + issues.front().message);
    return project;
}
} // namespace OpenModel
