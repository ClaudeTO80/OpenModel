#pragma once

#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshImporter.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
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
 * A model holds the loads once, and jobs select them per step. A job is one solver session; its steps run in order.
 * Script syntax (one command per line, `#` starts a comment, names must not contain blanks):
 * @code
 * material E=210000 nu=0.3 rho=7.85e-9 [name=STEEL]
 * material id=ALU E=70000 nu=0.33 rho=2.7e-9        # further materials (after the first one)
 * assign Housing material=ALU                         # material of one volume part (mesh block)
 * fix plane:x=0 [dof=1,2,3] [value=0,0,0] [tol=1e-6] # prescribed ux,uy,uz; defaults to zero
 * fix group:BladeRoot_NODES dof=1,2,3           # Node Set generated from a CAD face group
 * force plane:x=10 F=0,-100,0                   # total force, shared equally by the selected nodes
 * pressure group:Top p=2.5                      # on a dimension-2 surface group; positive = pushing into the body
 * distributed group:Top F=0,0,-40 [amp=NAME]    # total force of a dimension-2 group, shared by its faces in proportion to area
 * gravity g=0,0,-9810                           # acceleration vector (needs rho)
 * job SETTLE                                    # starts a job (one solver session); steps below belong to it
 * step static loads=F1,P1                       # only the listed loads are active; ids come from the lines above
 * step static loads=F2 reset                    # F1 and P1 of the previous steps are set to zero here
 * step frequency modes=6                        # or: step dynamic dt=0.01 period=1
 * @endcode
 * Without a `job` line the steps form one job named `job`; without `loads=` a step uses every load. Gravity is active
 * with the other loads, under the id `gravity`.
 * Node selectors: `all`, `group:NAME` (node set, generic mesh group, or the nodes of a physical group of any dimension) and
 * `plane:x=VALUE` (also y, z): nodes whose coordinate lies within `tol` of VALUE (default 1e-6 times the model diagonal).
 * Units are the user's: use a consistent set (for example mm, N, tonne, s as in the examples).
 */

namespace OpenModel {

enum class SelectorKind {
    All, Group, Plane,
    Nodes   ///< An explicit list of node indices (`NodeSelector::nodes`). Not written by the script syntax: it is made by withMeshConstraints().
};

/** @brief Which nodes a condition applies to. */
struct NodeSelector {
    SelectorKind kind = SelectorKind::All;
    std::string name;          ///< Group or node set name (kind Group).
    int axis = 0;              ///< 0, 1, 2 for x, y, z (kind Plane).
    double position = 0.0;     ///< Plane coordinate.
    double tolerance = -1.0;   ///< Negative = automatic.
    std::vector<NodeIndex> nodes; ///< Node indices (kind Nodes).
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

/**
 * @brief Rigid coupling of a set of nodes to a reference point (CalculiX `*RIGID BODY`), typically a bolt hole.
 *
 * The reference node is not a mesh node: it is created in the solver deck at `reference`, with the next free node id.
 * All six degrees of freedom of the slave nodes follow the reference node (the coupling has no per-direction mask,
 * because `*RIGID BODY` does not offer one). Only volume elements take part in the deck, so the slave nodes must
 * belong to solid elements. A node may belong to at most one link.
 */
struct RigidLink {
    std::string id;                    ///< Stable condition identifier (generated for holes: "rbe2-N").
    std::array<double, 3> reference{{0.0, 0.0, 0.0}}; ///< Position of the reference node, model length units.
    std::vector<NodeIndex> slaves;     ///< Mesh nodes coupled to the reference node.
    /// Node group the slaves come from (script `rbe2 group:NAME ref=...`). When set, the slaves are resolved from the
    /// group when the deck is written, and the script form is written; a link without a group keeps its slaves only in memory.
    std::string group;
};

/**
 * @brief Circle fitted to the nodes of a hole edge: centre, unit axis, radius and the worst relative deviation of the
 * nodes from an ideal circle (radial and axial).
 */
struct HoleFit {
    std::array<double, 3> centre{{0.0, 0.0, 0.0}};
    std::array<double, 3> axis{{0.0, 0.0, 1.0}};
    double radius = 0.0;
    double maxDeviation = 0.0;         ///< Relative to the radius; 0 for a perfect circle.
};

/**
 * @brief Time dependence of a load: a piecewise linear table of (time, factor) pairs. The load is the nominal value
 * multiplied by the factor. Written to the solver as *AMPLITUDE, referenced by the loads through AMPLITUDE=.
 */
struct AnalysisAmplitude {
    std::string name;                                ///< Identifier used by the loads (amp=NAME).
    std::vector<std::array<double, 2>> points;       ///< (time, factor), strictly increasing in time.
};

struct NodalForce {
    std::string id;             ///< Stable condition identifier.
    NodeSelector where;
    std::array<double, 3> total{{0.0, 0.0, 0.0}};   ///< Whole force; each selected node gets total / count.
    std::string amplitude;      ///< Optional amplitude name (empty = constant load).
};

struct SurfacePressure {
    std::string id;             ///< Stable condition identifier.
    std::string group;         ///< Physical group of surface elements (dimension 2).
    double pressure = 0.0;     ///< Positive acts into the body (compressive), as in Abaqus and CalculiX.
    std::string amplitude;     ///< Optional amplitude name (empty = constant load).
};

/**
 * @brief A total force distributed over a set of boundary faces.
 *
 * The faces of `group` (dimension 2) share the force in proportion to their area, so the traction is uniform over the
 * set (total / area). Each face hands its share to its corner nodes. The force is in global axes; the units are the
 * model's own, and keeping them consistent is up to whoever builds the model.
 */
struct DistributedLoad {
    std::string id;                               ///< Stable condition identifier.
    std::string group;                            ///< Physical group of surface elements (dimension 2): the faces.
    std::array<double, 3> total{{0.0, 0.0, 0.0}}; ///< Whole force of the set.
    std::string amplitude;                        ///< Optional amplitude name (empty = constant load).
};

enum class StepType {
    Static,     ///< *STATIC: linear static analysis, the loads are ramped over the step.
    Frequency,  ///< *FREQUENCY: natural modes; loads are ignored.
    Dynamic     ///< *DYNAMIC: transient analysis with an implicit time integration.
};

/** Identifier of the gravity load in AnalysisStep::loads (gravity is one load among the others). */
inline constexpr const char* kGravityLoadId = "gravity";

/**
 * @brief One step of a job: its analysis type, the loads that are active in it, and what happens to the loads of the
 * previous steps of the same job.
 *
 * A load that is active in a step is written with its value. A load of an earlier step that is not active here either
 * keeps its last value (`resetLoads` = false: the solver carries it over, as CalculiX does) or is set to zero
 * (`resetLoads` = true). Supports (`fix`) are not steps: they hold in every step of every job.
 */
struct AnalysisStep {
    StepType type = StepType::Static;
    std::size_t modes = 6;     ///< Frequency step: number of eigenvalues requested.
    /** Dynamic step (times in the model time unit). The minimum and maximum increments are optional: 0 = solver default. */
    double dynamicIncrement = 0.0;
    double dynamicPeriod = 0.0;
    double dynamicMinIncrement = 0.0;
    double dynamicMaxIncrement = 0.0;
    /** True: every load of the model is active in the step (the default). False: only `loads` are active. */
    bool allLoads = true;
    std::vector<std::string> loads;   ///< Ids of the active loads (forces, pressures, distributed, kGravityLoadId) when !allLoads.
    bool resetLoads = false;          ///< Set the loads of previous steps that are not active here to zero.
};

/**
 * @brief A job is one solver session: its steps run in order in the same input deck. The loads, materials, amplitudes and
 * supports are shared by all jobs of the model; each job selects the loads of its own steps.
 */
struct AnalysisJob {
    std::string id;                     ///< Stable name; it names the solver input and the output of the job.
    std::vector<AnalysisStep> steps;    ///< At least one step.
};

/** @brief Material of one volume part of the mesh (a mesh element block), set by an `assign` line. */
struct PartMaterial {
    std::string part;        ///< Name of a volume element block of the mesh.
    std::string materialId;  ///< Id of a material of the model.
};

struct AnalysisModel {
    AnalysisMaterial material;                  ///< Default material: every volume part without an assignment.
    std::vector<AnalysisMaterial> materials;    ///< Further materials, from the `material` lines after the first.
    std::vector<PartMaterial> partMaterials;    ///< `assign` lines: at most one material per volume part.
    std::vector<FixedSupport> supports;
    std::vector<NodalForce> forces;
    std::vector<SurfacePressure> pressures;
    std::vector<DistributedLoad> distributed;   ///< `distributed` lines: a total force over a face group.
    std::vector<RigidLink> rigidLinks; ///< Written after the supports; shared by all jobs.
    bool hasGravity = false;
    std::array<double, 3> gravity{{0.0, 0.0, 0.0}};
    std::vector<AnalysisAmplitude> amplitudes;
    /** Print element stresses/strains to the `.dat` (per-element results). Large for big meshes; -1 = automatic (<= 200000 elements). */
    int elementPrint = -1;
    /** Request the nodal results (`*NODE FILE`: displacements, and stresses and strains in a static step) for the `.frd`.
     *  Independent of elementPrint. Off: the solver writes no nodal results, and the viewer has nothing to show. */
    bool nodalPrint = true;
    /** The solver sessions. Empty only in a model built in code before the first job is added. */
    std::vector<AnalysisJob> jobs;
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
/** @brief Removes an additional material and the assign lines that use it. The default material cannot be removed. */
bool eraseMaterial(AnalysisModel& model, const std::string& id);
bool updateSupport(AnalysisModel& model, const std::string& id, const FixedSupport& replacement, std::string& error);
bool updateForce(AnalysisModel& model, const std::string& id, const NodalForce& replacement, std::string& error);
bool updatePressure(AnalysisModel& model, const std::string& id, const SurfacePressure& replacement, std::string& error);

/** Remove a support, force or pressure by ID. Material is required and cannot be erased. */
bool eraseSupport(AnalysisModel& model, const std::string& id);
bool eraseForce(AnalysisModel& model, const std::string& id);
bool erasePressure(AnalysisModel& model, const std::string& id);

/** @brief Read a script (see the file comment). Errors carry the line number; all of them are collected. */
AnalysisParseResult parseAnalysisScript(const std::string& text);

/** @brief Id of the job `id` (empty = the only job of the model, an error when there are several). Null when absent. */
const AnalysisJob* findJob(const AnalysisModel& model, const std::string& id);

/** @brief Material with id `id` (the default material included). Null when absent. */
const AnalysisMaterial* findAnalysisMaterial(const AnalysisModel& model, const std::string& id);

/** @brief Material of the volume part `part`: its assignment, else the default material. */
const AnalysisMaterial& materialForPart(const AnalysisModel& model, const std::string& part);

/** @brief Ids of the loads active in `step`: its own list, or every load of the model when `step.allLoads`. */
std::vector<std::string> activeLoadIds(const AnalysisModel& model, const AnalysisStep& step);

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
 * structure); every volume block gets the material. The job `jobId` (empty = the only job) becomes one deck with one
 * `*STEP` per step, in order. The file is written to `path` (the job name is its stem).
 * Checks: material values, supports present for a static step, non-empty selections, surface groups that really
 * are boundary faces of exactly one volume element (tetrahedra and hexahedra), at least one volume element.
 * Not checked: whether the model is physically sensible, units, mesh quality.
 */
SolverInputResult writeSolverInput(const MeshData& mesh, const AnalysisModel& model, const std::filesystem::path& path,
                                   const std::string& jobId = {});

/**
 * @brief Detects conflicting prescribed translations of overlapping supports.
 *
 * Each (node, DOF) remembers the first support that constrained it and its value, so
 * registering a support costs O(selected nodes) instead of a scan of all previous
 * supports per node. Equal overlapping prescriptions are accepted.
 */
class PrescribedDisplacementTracker {
public:
    /**
     * @brief Register one support (call in script order).
     * @param supportIndex Zero-based index of the support, used in diagnostics as `fix #index+1`.
     * @param support      The support; its active DOFs are checked.
     * @param nodes        Nodes selected by the support.
     * @param mesh         Mesh, used to report the node ID.
     * @return Empty string on success, otherwise a diagnostic (non-finite value or conflict).
     */
    std::string add(std::size_t supportIndex, const FixedSupport& support,
                    const std::vector<NodeIndex>& nodes, const MeshData& mesh);

private:
    struct Entry {
        std::size_t support = static_cast<std::size_t>(-1); ///< First support constraining this DOF.
        double value = 0.0;
    };
    std::unordered_map<NodeIndex, std::array<Entry, 3>> m_entries;
};

/**
 * @brief The constraints that the supports (`fix`) of an analysis put on the nodes of a mesh.
 *
 * Each support is resolved with selectNodes(); the result has one entry per constrained node (ordered by node index),
 * with the degrees of freedom 1 to 3 that some support holds and the prescribed displacement of each. A support whose
 * selection is empty, or that prescribes a value different from an earlier support on the same node and degree of freedom,
 * is left out and reported in `diagnostics` as a warning (writeSolverInput() refuses the same cases with an error).
 * Meant to show or export what the supports do, without writing a solver deck.
 */
std::vector<NodeConstraint> supportConstraints(const MeshData& mesh, const AnalysisModel& model,
                                               std::vector<ImportDiagnostic>& diagnostics);

/**
 * @brief `model` plus the constraints stored in the mesh (MeshData::getConstraints()), as extra supports.
 *
 * The nodes that carry the same translations and the same prescribed values become one support (selector kind Nodes), added
 * after the supports of the script, so that the solver writers handle them like any `fix`. Only translations exist for the
 * volume elements the solvers get: a rotation in the mask is dropped and reported once as a warning. Adds an info line with the
 * number of nodes taken from the mesh. The result is a temporary model for writing a solver input, not something to serialize.
 */
AnalysisModel withMeshConstraints(const MeshData& mesh, const AnalysisModel& model, std::vector<ImportDiagnostic>& diagnostics);

/** @brief Add supportConstraints() to the constraints of `mesh` (a degree of freedom already constrained takes the new value). */
void applySupports(MeshData& mesh, const AnalysisModel& model, std::vector<ImportDiagnostic>& diagnostics);

/**
 * @brief Least-squares-style circle through the nodes of a hole edge.
 *
 * The centre is the centroid of the nodes and the axis is the direction of smallest spread of the nodes (the normal of
 * the plane of the edge). The radius is the mean in-plane distance from the centre. Returns false when fewer than three
 * nodes are given or when they all coincide. Exact for a uniformly meshed ring; for an irregular ring the centroid is
 * an approximation and `maxDeviation` shows how far the nodes are from a circle.
 */
bool fitHoleCircle(const MeshData& mesh, const std::vector<NodeIndex>& edge, HoleFit& fit);

/**
 * @brief One RigidLink per hole: each hole edge is fitted with fitHoleCircle() and coupled to `reference`.
 *
 * The reference point is the one the user picks outside the part. Warnings (not errors): a reference farther than 5 % of
 * the radius from the hole axis (`RBE2_OFF_AXIS`), a hole whose nodes deviate more than 5 % from a circle
 * (`RBE2_NOT_CIRCULAR`). Ids are "rbe2-1", "rbe2-2", ... in the order of `holes`. Returns an empty vector and sets
 * `error` when a hole cannot be fitted.
 */
std::vector<RigidLink> holeRigidLinks(const MeshData& mesh, const std::vector<std::vector<NodeIndex>>& holes,
                                      const std::array<double, 3>& reference, std::vector<ImportDiagnostic>& diagnostics,
                                      std::string& error);

/** @brief Resolve a selector to node indices (sorted, unique). Empty result + `error` when nothing matches or the name is unknown. */
std::vector<NodeIndex> selectNodes(const MeshData& mesh, const NodeSelector& selector, std::string& error);

} // namespace OpenModel
