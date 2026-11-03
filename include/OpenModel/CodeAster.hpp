#pragma once

#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/FemResults.hpp>
#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>

#include <filesystem>
#include <string>
#include <vector>

/**
 * @file CodeAster.hpp
 * @brief code_aster support: case export, mesh import, launch command and result tables.
 *
 * ### Export
 * writeCodeAsterCase() turns a MeshData and an AnalysisModel (the same model that drives the CalculiX writer) into three files
 * in a folder: `<job>.mail` (mesh in the code_aster text format "ASTER"), `<job>.comm` (command file) and `<job>.export`
 * (the file given to `run_aster` / `as_run`). Node `N<id>` and element `M<id>` carry the external ids of the mesh, so results
 * map back to the mesh by id. Physical groups and generic mesh-group element memberships become `GROUP_MA`, node sets and generic mesh-group node memberships become `GROUP_NO`; generic group node memberships use `<name>_NODES`. The analysis adds `OM_VOLUME`
 * (all volume elements), `OM_FIX<i>` and `OM_LOAD<i>`.
 *
 * ### Results
 * The command file writes plain text tables (`IMPR_TABLE`, `FORMAT='TABLEAU'`, separator `,`) to these files, which
 * CodeAsterTableImporter reads (extension `.tab`):
 *  - `<job>_disp.tab`, `<job>_stress.tab`, `<job>_strain.tab` for a static analysis (nodal `DEPL`, `SIGM_NOEU`, `EPSI_NOEU`);
 *  - `<job>_modes_disp.tab` and `<job>_modes.tab` (mode shapes; `NUME_ORDRE,FREQ`) for a frequency analysis.
 * Fields are renamed to the names the result binder knows: `DISP` (D1..D3), `STRESS` (SXX SYY SZZ SXY SYZ SZX),
 * `STRAIN` (EXX EYY EZZ EXY EYZ EZX, tensor shear components).
 */

namespace OpenModel {

/** @brief Settings of the generated code_aster case. */
struct CodeAsterOptions {
    std::string jobName = "job";        ///< Stem of the generated files (letters, digits, `_`, `-`).
    int timeLimitSeconds = 3600;        ///< `time_limit` of the export file.
    int memoryLimitMB = 2048;           ///< `memory_limit` of the export file.
    int ncpus = 1;                      ///< `ncpus` of the export file.
    std::string version;                ///< Optional `version` line of the export file (empty = installation default).
    /** Linear solver of the static analysis: `MULT_FRONT` (always available), `MUMPS`, `PETSC`, `LDLT`. */
    std::string solver = "MULT_FRONT";
};

/** @brief Outcome of writeCodeAsterCase(). */
struct CodeAsterExportResult {
    bool success = false;
    std::string error;
    std::vector<ImportDiagnostic> diagnostics;   ///< Warnings (and context of an error).
    std::filesystem::path meshFile, commFile, exportFile;
    std::size_t nodes = 0, elements = 0;         ///< Written to the mesh file (all dimensions).
    std::size_t volumeElements = 0;
    std::size_t fixedNodes = 0, loadedNodes = 0, pressureFaces = 0;
    std::string summary;
};

/**
 * @brief Write `<job>.mail`, `<job>.comm` and `<job>.export` for `mesh` and `model` into `directory` (created when missing).
 *
 * Checks as for writeSolverInput() (material, supports for a static step, non-empty selections, pressure surfaces that are
 * boundary faces of exactly one volume element) plus: node and element ids must have at most 7 digits (code_aster names
 * are 8 characters), and the element types must be supported (everything except 27-node hexahedra).
 * Loads follow the CalculiX writer: a force is shared equally by the selected nodes (`FORCE_NODALE`), pressure is positive
 * into the body (`PRES_REP`), gravity is an acceleration (`PESANTEUR`), selected nodes are restricted to those of volume elements.
 */
CodeAsterExportResult writeCodeAsterCase(const MeshData& mesh, const AnalysisModel& model,
                                         const std::filesystem::path& directory, const CodeAsterOptions& options = {});

/**
 * @brief Write only the mesh in the code_aster "ASTER" text format.
 * @return Empty string on success, otherwise the error message.
 */
std::string writeCodeAsterMesh(const std::filesystem::path& path, const MeshData& mesh);

/**
 * @brief Find the code_aster launcher: `hint` when given, else the environment variable `OPENMODEL_CODE_ASTER`, else
 *        `run_aster` and then `as_run` in `PATH`. Returns an empty string when none is found.
 */
std::string findCodeAsterExecutable(const std::string& hint = {});

/** @brief Command line that runs an export file: `{launcher, "<file>.export"}` (to be started in the export file's folder). */
std::vector<std::string> codeAsterLaunchCommand(const std::string& launcher, const std::filesystem::path& exportFile);

/** @brief Result tables of the job that exist and are not empty, in the order they should be merged (`.tab`). */
std::vector<std::filesystem::path> codeAsterResultFiles(const std::filesystem::path& directory, const std::string& jobName);

/** @brief Remove the result tables and message file of a previous run of the job (so that stale output is not mistaken for new). */
void removeCodeAsterOutputs(const std::filesystem::path& directory, const std::string& jobName);

/**
 * @brief Reader of the code_aster text mesh format ("ASTER", `.mail`).
 *
 * Reads `COOR_3D` / `COOR_2D`, the element blocks (POI1, SEG2, SEG3, TRIA3, TRIA6, QUAD4, QUAD8, QUAD9, TETRA4, TETRA10, HEXA8,
 * HEXA20, PENTA6, PENTA15, PYRAM5), `GROUP_NO NOM=...` (node sets) and `GROUP_MA NOM=...` (physical groups, split by dimension
 * when mixed). A name such as `N12` / `M7` gives the id 12 / 7; if the names have no unique number the entities are numbered
 * from 1 and a warning says so. Not supported: MED files, 27-node hexahedra, discrete elements, `FINSF`-less variants of the
 * old format, `SOUS_STRUC`, `NOM=` on a separate line. Unknown blocks are skipped with a warning.
 */
class CodeAsterMeshImporter final : public MeshImporter {
public:
    std::string formatName() const override;
    bool canRead(const std::filesystem::path& path) const override;
    MeshImportResult read(const std::filesystem::path& path, MeshData& destination,
                          const MeshImportOptions& options = {}) const override;
};

/**
 * @brief Reader of the tables written by the generated command file (`.tab`).
 *
 * Accepts the output of `IMPR_TABLE(FORMAT='TABLEAU')` with any of the separators comma, semicolon, tab or blanks; the header
 * may start with `#`. Recognised columns: `NOEUD` / `NODE` (node name `N12` or plain `12`), `NUME_ORDRE`, `NUME_MODE`, `INST`, `FREQ`;
 * `DX DY DZ` become `DISP`, `SIXX SIYY SIZZ SIXY SIXZ SIYZ` become `STRESS`, `EPXX ...` become `STRAIN`; every other
 * numeric column of a nodal table is stored as a scalar field under its own name. A table without node column that has
 * `FREQ` is the eigenvalue table. A file whose name ends with `_modes` (or `_modes_disp`, ...) holds one vibration mode per
 * `NUME_ORDRE`; otherwise each distinct `NUME_ORDRE` (or `INST`) is a load/time step and a single one is a static step.
 */
class CodeAsterTableImporter final : public ResultImporter {
public:
    std::string formatName() const override;
    bool canRead(const std::filesystem::path& path) const override;
    ResultImportResult read(const std::filesystem::path& path, FemResults& destination) const override;
    ResultImportResult parse(std::istream& input, FemResults& destination) const override;
    /** @brief Same as parse(); `modal` tells that every order is a vibration mode (see the class comment). */
    ResultImportResult parse(std::istream& input, FemResults& destination, bool modal) const;
};

} // namespace OpenModel
