#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace OpenModel {

enum class ProjectAssetStorage { Managed, Linked };

struct ProjectAsset {
    std::string id;
    std::string role;
    std::filesystem::path path;
    ProjectAssetStorage storage = ProjectAssetStorage::Managed;
    std::string mediaType;
    std::uintmax_t sizeBytes = 0;
    std::string sha256;
};

struct ProjectMetadata {
    std::string id;
    std::string name;
    std::string description;
    std::string applicationVersion;
    std::string createdUtc;
    std::string modifiedUtc;
};

struct ProjectValidationIssue {
    std::string assetId;
    std::string message;
};

class OpenModelProject {
public:
    static constexpr unsigned schemaMajor = 1;
    static constexpr unsigned schemaMinor = 0;

    ProjectMetadata metadata;
    std::vector<ProjectAsset> assets;

    void addAsset(ProjectAsset asset);
    bool removeAsset(const std::string& id);
    const ProjectAsset* findAsset(const std::string& id) const noexcept;
    ProjectAsset* findAsset(const std::string& id) noexcept;

    std::vector<ProjectValidationIssue> validate(const std::filesystem::path& manifestPath) const;
    std::filesystem::path resolveAssetPath(const std::filesystem::path& manifestPath,
                                           const ProjectAsset& asset) const;

    void save(const std::filesystem::path& manifestPath) const;
    static OpenModelProject load(const std::filesystem::path& manifestPath);
};

} // namespace OpenModel
