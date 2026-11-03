#pragma once

#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

/**
 * @file AnalysisSetup.hpp
 * @brief Definition of a linear-elastic analysis (material, supports, loads, step) and its CalculiX input file.
 *
 * OpenModel meshes carry no material, boundary condition or load. An AnalysisModel adds them, either built in code or
 * read from a short text script (parseAnalysisScript), and writeSolverInput() turns mesh + model into a complete
 * CalculiX `.inp` that requests exactly the output the result readers understand (nodal U/S/E in the `.frd`,
 * S/E per element in the `.dat`).
 *
 * Script syntax (one command per line, `#` starts a comment, names must not contain blanks):
 * @code
 * material E=210000 nu=0.3 rho=7.85e-9 [name=STEEL]
 * fix plane:x=0 [dof=1,2,3] [value=0,0,0] [tol=1e-6] # prescribed ux,uy,uz; defaults to zero
 * fix group:BladeRoot_NODES dof=1,2,3           # Node Set generated from a CAD face group
 * force plane:x=10 F=0,-100,0                   # total force, shared equally by the selected nodes
 * pressure group:Top p=2.5                      # on a dimension-2 surface group; positive = pushing into the body
 * gravity g=0,0,-9810                           # acceleration vector (needs rho)
 * step static                                   # or: step frequency modes=6
 * @endcode
 * Node selectors: `all`, `group:NAME` (node set, generic mesh group, or the nodes of a physical group of any dimension) and
 * `plane:x=VALUE` (also y, z): nodes whose coordinate lies within `tol` of VALUE (default 1e-6 times the model diagonal).
 * Units are the user's: use a consistent set (for example mm, N, tonne, s as in the examples).
 */

namespace OpenModel {

enum class SelectorKind { All, Group, Plane };

/** @brief Which nodes a condition applies to. */
struct NodeSelector {
    SelectorKind kind = SelectorKind::All;
    std::string name;          ///< Group or node set name (kind Group).
    int axis = 0;              ///< 0, 1, 2 for x, y, z (kind Plane).
    double position = 0.0;     ///< Plane coordinate.
    double tolerance = -1.0;   ///< Negative = automatic.
};

struct AnalysisMaterial {
    std::string id;             ///< Stable condition identifier; generated when omitted from the script.
    std::string name = "MATERIAL";
    double youngModulus = 0.0;
    double poisson = 0.0;
    double density = 0.0;      ///< Needed for frequency analysis and gravity.
};

struct FixedSupport {
    std::string id;             ///< Stable condition identifier.
    NodeSelector where;
    std::array<bool, 3> dof{{true, true, true}};
    /** Prescribed translations in model length units; zero preserves fixed-support behaviour. */
    std::array<double, 3> displacement{{0.0, 0.0, 0.0}};
};

struct NodalForce {
    std::string id;             ///< Stable condition identifier.
    NodeSelector where;
    std::array<double, 3> total{{0.0, 0.0, 0.0}};   ///< Whole force; each selected node gets total / count.
};

struct SurfacePressure {
    std::string id;             ///< Stable condition identifier.
    std::string group;         ///< Physical group of surface elements (dimension 2).
    double pressure = 0.0;     ///< Positive acts into the body (compressive), as in Abaqus and CalculiX.
};

enum class StepType { Static, Frequency };

struct AnalysisModel {
    AnalysisMaterial material;
    std::vector<FixedSupport> supports;
    std::vector<NodalForce> forces;
    std::vector<SurfacePressure> pressures;
    bool hasGravity = false;
    std::array<double, 3> gravity{{0.0, 0.0, 0.0}};
    StepType step = StepType::Static;
    std::size_t modes = 6;     ///< Frequency step: number of eigenvalues requested.
    /** Print element stresses/strains to the `.dat` (per-element results). Large for big meshes; -1 = automatic (<= 200000 elements). */
    int elementPrint = -1;
};

struct AnalysisParseResult {
    bool success = false;
    AnalysisModel model;
    std::vector<ImportDiagnostic> diagnostics;
};


/** ID-based condition access. Returned pointers remain valid only until the corresponding container is modified. */
FixedSupport* findSupport(AnalysisModel& model, const std::string& id);
NodalForce* findForce(AnalysisModel& model, const std::string& id);
SurfacePressure* findPressure(AnalysisModel& model, const std::string& id);
const FixedSupport* findSupport(const AnalysisModel& model, const std::string& id);
const NodalForce* findForce(const AnalysisModel& model, const std::string& id);
const SurfacePressure* findPressure(const AnalysisModel& model, const std::string& id);

/** Replace a condition while preserving its ID. Returns false and sets error if the ID is absent or invalid. */
bool updateMaterial(AnalysisModel& model, const std::string& id, const AnalysisMaterial& replacement, std::string& error);
bool updateSupport(AnalysisModel& model, const std::string& id, const FixedSupport& replacement, std::string& error);
bool updateForce(AnalysisModel& model, const std::string& id, const NodalForce& replacement, std::string& error);
bool updatePressure(AnalysisModel& model, const std::string& id, const SurfacePressure& replacement, std::string& error);

/** Remove a support, force or pressure by ID. Material is required and cannot be erased. */
bool eraseSupport(AnalysisModel& model, const std::string& id);
bool eraseForce(AnalysisModel& model, const std::string& id);
bool erasePressure(AnalysisModel& model, const std::string& id);

/** @brief Read a script (see the file comment). Errors carry the line number; all of them are collected. */
AnalysisParseResult parseAnalysisScript(const std::string& text);

/** @brief Serialize an analysis model to a round-trippable script, including stable condition IDs. */
std::string serializeAnalysisScript(const AnalysisModel& model);

/** @brief A small example script for a static analysis, for the user interfaces. */
std::string exampleAnalysisScript();

struct SolverInputResult {
    bool success = false;
    std::string error;
    std::vector<ImportDiagnostic> diagnostics;   ///< Warnings (and context of an error).
    std::size_t nodes = 0, elements = 0;         ///< Volume elements written.
    std::size_t fixedNodes = 0;                  ///< Nodes with at least one constrained translation.
    std::size_t loadedNodes = 0;
    std::size_t pressureFaces = 0;
    std::string summary;                         ///< One paragraph describing what was written.
};

/**
 * @brief Write the complete CalculiX input for `mesh` and `model`.
 *
 * Only the volume elements are written (surface and line elements of the mesh are boundary descriptions, not
 * structure); every volume block gets the material. The file is written to `path` (the job name is its stem).
 * Checks: material values, supports present for a static step, non-empty selections, surface groups that really
 * are boundary faces of exactly one volume element (tetrahedra and hexahedra), at least one volume element.
 * Not checked: whether the model is physically sensible, units, mesh quality.
 */
SolverInputResult writeSolverInput(const MeshData& mesh, const AnalysisModel& model, const std::filesystem::path& path);

/** @brief Resolve a selector to node indices (sorted, unique). Empty result + `error` when nothing matches or the name is unknown. */
std::vector<NodeIndex> selectNodes(const MeshData& mesh, const NodeSelector& selector, std::string& error);

} // namespace OpenModel
