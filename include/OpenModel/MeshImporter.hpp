#pragma once

#include <OpenModel/MeshData.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace OpenModel {

enum class ImportStatus {
    Success,
    IoError,
    InvalidInput,
    UnsupportedFormat,
    UnsupportedFeature
};

enum class ImportDiagnosticLevel { Info, Warning, Error };

struct ImportDiagnostic {
    ImportDiagnosticLevel level = ImportDiagnosticLevel::Info;
    std::string code;
    std::string message;
    std::size_t line = 0;
};

struct MeshImportOptions {
    bool importNodeSets = true;
    bool importElementSets = true;
    bool importPhysicalGroups = true;
};

struct MeshImportResult {
    ImportStatus status = ImportStatus::Success;
    std::vector<ImportDiagnostic> diagnostics;
    std::size_t nodesRead = 0;
    std::size_t elementsRead = 0;
    std::size_t nodeSetsRead = 0;
    std::size_t elementSetsRead = 0;

    explicit operator bool() const noexcept { return status == ImportStatus::Success; }
    void addError(ImportStatus newStatus, std::string code,
                  std::string message, std::size_t line = 0);
    void addWarning(std::string code, std::string message, std::size_t line = 0);
};

class MeshImporter {
public:
    virtual ~MeshImporter() = default;
    virtual std::string formatName() const = 0;
    virtual bool canRead(const std::filesystem::path& path) const = 0;
    virtual MeshImportResult read(const std::filesystem::path& path,
                                  MeshData& destination,
                                  const MeshImportOptions& options = {}) const = 0;
};

class AbaqusInpImporter final : public MeshImporter {
public:
    std::string formatName() const override;
    bool canRead(const std::filesystem::path& path) const override;
    MeshImportResult read(const std::filesystem::path& path, MeshData& destination,
                          const MeshImportOptions& options = {}) const override;
};

class CalculixInpImporter final : public MeshImporter {
public:
    std::string formatName() const override;
    bool canRead(const std::filesystem::path& path) const override;
    MeshImportResult read(const std::filesystem::path& path, MeshData& destination,
                          const MeshImportOptions& options = {}) const override;
};

} // namespace OpenModel
