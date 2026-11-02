#include <OpenModel/MeshImporter.hpp>
#include "importers/InpImporterCommon.hpp"

namespace OpenModel {
std::string AbaqusInpImporter::formatName() const { return "Abaqus INP"; }
bool AbaqusInpImporter::canRead(const std::filesystem::path& path) const {
    return detail::upper(path.extension().string()) == ".INP";
}
MeshImportResult AbaqusInpImporter::read(const std::filesystem::path& path, MeshData& destination,
                                         const MeshImportOptions& options) const {
    return detail::readInp(path, destination, options, "Abaqus INP");
}
} // namespace OpenModel
