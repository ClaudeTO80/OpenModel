#pragma once

#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>

#include <filesystem>
#include <string>
#include <vector>

/**
 * @file ElmerSolver.hpp
 * @brief Elmer support: case export of a static analysis (mesh in the ElmerSolver format, SIF input file) and launch command.
 *
 * ### Export
 * writeElmerCase() writes, in one folder, the mesh in the ElmerSolver directory format (`mesh.header`, `mesh.nodes`,
 * `mesh.elements`, `mesh.boundary`) and the solver input `case.sif`. The solver is `ElmerSolver case.sif`, started in
 * the folder of the case: it reads the mesh from `.`; the results are `<job>.result` (the output file) and `<job>.vtu`
 * (the post file), named after the job so that several jobs of one model do not overwrite each other.
 *
 * Mapping of the analysis script (static steps only, one step per job):
 *  - each volume element block is a Body; its material is materialForPart() (per-part `assign` lines);
 *  - `material` gives Youngs Modulus, Poisson ratio and Density;
 *  - `gravity` gives a Stress Bodyforce on every body (density needed);
 *  - `fix` gives Displacement components on point elements (one boundary per fix; free directions are not set);
 *  - `pressure` gives a Normal Force on the boundary faces of its surface group (linear triangles and quadrilaterals only).
 * Not written for Elmer yet, and refused with a message: nodal `force`, `distributed` loads, `rbe2` rigid links,
 * time-dependent loads (amplitudes), frequency and dynamic steps, several steps per job.
 *
 * ### Not verified
 * ElmerSolver is not installed where this code was written: the mesh and the SIF follow the keywords of the Elmer manual
 * as remembered (point elements for fixes, Normal Force sign, node order of the element types, Stress Bodyforce names).
 * Tests check the files written, not the solution. Results of Elmer are not read back into the viewer yet.
 */

namespace OpenModel {

/** @brief Outcome of writeElmerCase(). */
struct ElmerExportResult {
    bool success = false;
    std::string error;
    std::vector<ImportDiagnostic> diagnostics;   ///< Warnings (and context of an error).
    std::filesystem::path sifFile;                ///< `<job>.sif` in the case folder.
    std::size_t nodes = 0;
    std::size_t volumeElements = 0;
    std::size_t boundaryElements = 0;             ///< Point elements of the fixes and face elements of the pressures.
    std::size_t bodies = 0;
    std::string summary;
};

/**
 * @brief Write the Elmer case of `model`'s job `jobId` (empty = the only job) for `mesh` into `directory`.
 *
 * The SIF is named after the job (`<job>.sif`). Checks: a static single-step job; a material with positive Young's modulus and Poisson's ratio in (-1, 0.5); a support
 * for the solid; gravity only with a density; assignments naming existing volume blocks; pressure groups that are linear
 * surface groups matching exactly one volume face each; at least one volume element. Node numbering: the mesh nodes are
 * numbered 1..N in their order; element numbering is sequential over the volume blocks, then the boundary elements.
 */
ElmerExportResult writeElmerCase(const MeshData& mesh, const AnalysisModel& model, const std::filesystem::path& directory,
                                 const std::string& jobId = {});

/**
 * @brief Find the Elmer solver: `hint` when given (taken as it is), else the environment variable `OPENMODEL_ELMER`, else
 *        `ElmerSolver` in `PATH`. Returns an empty string when none is found.
 */
std::string findElmerExecutable(const std::string& hint = {});

/** @brief Command line that solves a case: `{launcher, "<job>.sif"}`, started in the folder of the case. */
std::vector<std::string> elmerLaunchCommand(const std::string& launcher, const std::filesystem::path& sifFile);

} // namespace OpenModel
