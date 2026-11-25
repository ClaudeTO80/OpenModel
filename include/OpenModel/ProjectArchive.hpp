#pragma once

#include <OpenModel/OpenModelProject.hpp>

#include <filesystem>
#include <stdexcept>

namespace OpenModel {

/**
 * @brief Self-contained ZIP archive of an OpenModel project.
 *
 * An archive holds the project manifest and every asset payload, so a project can be moved as one file. The archive
 * is a standard ZIP file with every entry stored without compression (method 0). It uses no ZIP64 extensions and no
 * archive comment, so it is limited to 4 GiB per entry and 65,534 entries; larger projects are refused.
 *
 * Layout of the archive:
 * - `project.omproj`: the manifest, with every asset path rewritten to its archive path and storage `managed`;
 * - `assets/<asset id>/<file name>`: the payload of each asset, including linked assets, which are copied in.
 *
 * Asset IDs must not contain path separators, colons, control characters, or be `.` or `..`.
 *
 * The analysis script is not special: it is a text asset with role `analysis.model`. Its jobs, steps and loads are
 * written to the script, so they travel with the archive once the script is saved as an asset of the project.
 *
 * The archive is written to a temporary sibling file and renamed over the destination. The reader checks every CRC-32
 * and every SHA-256 recorded in the manifest, and rejects paths that would leave the destination directory.
 */
class ProjectArchiveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Name of the manifest entry, inside the archive and after import.
inline constexpr const char* kProjectArchiveManifestName = "project.omproj";

/**
 * @brief Pack the project described by `manifest` and all of its assets into the ZIP file `archive`.
 *
 * Refuses (ProjectArchiveError) when the manifest does not validate, an asset file is missing, a recorded size or
 * SHA-256 does not match the file, or the archive would need ZIP64. Nothing is written in those cases.
 */
void exportProjectArchive(const std::filesystem::path& manifest, const std::filesystem::path& archive);

/**
 * @brief Extract the archive `archive` into the new directory `destination` and return the path of its manifest.
 *
 * Refuses (ProjectArchiveError) an archive that is compressed, encrypted, uses ZIP64 or an archive comment, has a
 * corrupted entry, an unsafe entry name, or a destination that already holds a manifest. On any failure, the files
 * written so far are removed.
 */
std::filesystem::path importProjectArchive(const std::filesystem::path& archive, const std::filesystem::path& destination);

} // namespace OpenModel
