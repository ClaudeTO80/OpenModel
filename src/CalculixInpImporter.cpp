#include <OpenModel/MeshImporter.hpp>
#include "importers/InpImporterCommon.hpp"

namespace OpenModel {
std::string CalculixInpImporter::formatName() const { return "CalculiX INP"; }
bool CalculixInpImporter::canRead(const std::filesystem::path& path) const {
    return detail::upper(path.extension().string()) == ".INP";
}
MeshImportResult CalculixInpImporter::read(const std::filesystem::path& path, MeshData& destination,
                                          const MeshImportOptions& options) const {
    // CalculiX shares much of the Abaqus keyword syntax, but remains a distinct importer
    // so solver-specific keyword/type handling can evolve independently.
    return detail::readInp(path, destination, options, "CalculiX INP");
}
} // namespace OpenModel
