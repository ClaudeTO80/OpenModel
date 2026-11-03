#include <OpenModel/OpenModelProject.hpp>

#include <algorithm>
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
