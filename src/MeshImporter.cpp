#include <OpenModel/MeshImporter.hpp>

namespace OpenModel {
void MeshImportResult::addError(ImportStatus newStatus, std::string code,
                                std::string message, std::size_t line) {
    if (status == ImportStatus::Success) status = newStatus;
    diagnostics.push_back({ImportDiagnosticLevel::Error, std::move(code), std::move(message), line});
}
void MeshImportResult::addWarning(std::string code, std::string message, std::size_t line) {
    diagnostics.push_back({ImportDiagnosticLevel::Warning, std::move(code), std::move(message), line});
}
} // namespace OpenModel
