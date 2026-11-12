#pragma once
#include <memory>
#include <string>
#include <vector>
#include <OpenModel/CADModel.hpp>
#include <OpenModel/MeshData.hpp> // La classe descritta sopra

enum class MeshDimension { Surface, Volume };

/** @brief How the meshes of different CAD solids relate to each other where the solids touch. */
enum class InterfaceMode {
    /** Each solid is meshed on its own: touching faces get unrelated triangulations and distinct nodes. */
    Independent,
    /** Touching solids share their interface: one face mesh, nodes shared by both sides (CAD fragment). */
    Conformal
};

/** @brief Topology of a volume mesh (Gmsh backend only). */
enum class MeshStructure {
    /** Tetrahedral mesh of any geometry (the default). */
    Unstructured,
    /**
     * Structured mesh built with the structured-meshing tools of Gmsh. Every solid must be one of:
     *  - a hexahedral block (six four-sided faces, eight corners), meshed with transfinite curves, surfaces
     *    and volume: a regular grid of hexahedra or of tetrahedra;
     *  - a straight prism (two congruent parallel planar end faces joined by walls parallel to the sweep
     *    direction, e.g. beams, bars, cylinders), meshed by sweeping the mesh of one end face in layers.
     * Anything else cannot be meshed in a structured way; see MeshingOptions::structuredFallback.
     */
    Structured
};

/** @brief Element family of a structured volume mesh. */
enum class StructuredElementShape {
    /**
     * Only hexahedra: transfinite blocks recombined into hexahedra, and swept end faces recombined with a
     * full-quad algorithm so that no triangle (hence no prism) is left.
     */
    Hexahedron,
    /** Triangular end-face mesh swept into prisms (wedges); a hexahedral block is meshed as a straight prism. */
    Prism,
    /** Transfinite blocks meshed with a regular grid of tetrahedra; swept solids are layered into tetrahedra. */
    Tetrahedron,
    /**
     * Hexahedra where the solid allows it; a swept end face is recombined with the standard algorithm and the
     * triangles it leaves become prisms (hexahedra and prisms in one mesh).
     */
    Mixed
};

/** @brief Distribution of the nodes along a transfinite curve (`gmsh::model::mesh::setTransfiniteCurve`, `meshType`). */
enum class TransfiniteCurveType {
    /** Geometric progression with power `coef`: 1 is uniform, above 1 the elements grow along the curve. */
    Progression,
    /** Refinement toward both extremities of the curve. */
    Bump,
    /** Beta law. */
    Beta
};

/** @brief Arrangement of the triangles of an unrecombined transfinite surface (`setTransfiniteSurface`, `arrangement`). */
enum class TransfiniteArrangement { Left, Right, AlternateLeft, AlternateRight };

/** @brief Values of the Gmsh option `Mesh.RecombinationAlgorithm`. */
enum class RecombinationAlgorithm {
    /** Chosen from the element family: BlossomFullQuad (hexahedra only) for Hexahedron, Blossom for Mixed. */
    Automatic = -1,
    Simple = 0,
    Blossom = 1,
    SimpleFullQuad = 2,
    BlossomFullQuad = 3
};

/**
 * @brief The Gmsh settings that govern a structured mesh, and only those.
 *
 * Divisions are not set directly: the number of nodes of every transfinite curve (and the number of layers of a
 * swept solid) is the one that gives elements no longer than MeshingOptions::maxElementSize.
 */
struct StructuredGmshOptions {
    /** `setTransfiniteCurve` meshType: node distribution on every transfinite curve. */
    TransfiniteCurveType curveType = TransfiniteCurveType::Progression;
    /** `setTransfiniteCurve` coef: progression power or bump/beta coefficient; must be positive, 1 is uniform. */
    double curveCoefficient = 1.0;
    /** `setTransfiniteSurface` arrangement: how the quadrangles of an unrecombined surface are cut into triangles. */
    TransfiniteArrangement arrangement = TransfiniteArrangement::Left;
    /** `Mesh.RecombinationAlgorithm`; Automatic derives it from the element family. */
    RecombinationAlgorithm recombinationAlgorithm = RecombinationAlgorithm::Automatic;
    /** `setRecombine` angle in degrees: threshold angle of the simple recombination algorithm, in (0, 180]. */
    double recombineAngle = 45.0;
    /** `Mesh.RecombineOptimizeTopology`: number of topological optimisation passes after recombination, 0 or more. */
    int recombineOptimizeTopology = 5;
    /** `Mesh.RecombineNodeRepositioning`: reposition the nodes after recombination. */
    bool recombineNodeRepositioning = true;
    /** `Mesh.RecombineMinimumQuality`: minimum quality of a quadrangle created by the recombination, in [0, 1]. */
    double recombineMinimumQuality = 0.01;
};

struct MeshingOptions {
    MeshDimension dimension = MeshDimension::Volume;
    // Legacy volume target size; retained for source compatibility.
    double elementSize = 1.0;
    int order = 1; // 1 = linear, 2 = quadratic (backend dependent)
    // Gmsh-only controls; other meshing backends may ignore these.
    bool recombineSurfaceElements = false;
    bool incompleteSecondOrder = false; // Gmsh: omit quad interior nodes (Quad8 instead of Quad9).
    bool generateTetrahedra = true;

    // CAD tessellation controls. linearDeflection is a geometric chordal
    // tolerance, not a guaranteed triangle edge length.
    double linearDeflection = 0.1;
    double angularDeflection = 0.5; // radians for Open CASCADE
    double minElementSize = 0.0;   // 0 = backend default / no explicit minimum
    double maxElementSize = 0.0;   // 0 = use elementSize as the maximum
    bool curvatureAdaptation = true;
    double curvaturePointsPerCircle = 12.0; // hint for backends that support it

    /**
     * Gmsh only: volume mesh topology. Structured meshes have elements of about maxElementSize along
     * transfinite curves and sweep layers (far fewer elements than a tetrahedral mesh of the same size), keep
     * the order of the nodes of second-order hexahedra and prisms (20 and 15 nodes, no pyramids) and, when
     * a solid has to be swept, are not available together with InterfaceMode::Conformal and several solids.
     * Ignored for surface meshes.
     */
    MeshStructure structure = MeshStructure::Unstructured;
    /** Gmsh only: element family of a structured volume mesh. */
    StructuredElementShape structuredShape = StructuredElementShape::Hexahedron;
    /** Gmsh only: the settings of a structured mesh (transfinite curves and surfaces, recombination). */
    StructuredGmshOptions structuredGmsh;
    /**
     * Gmsh only: when a structured mesh is requested but a solid is neither a hexahedral block nor a straight
     * prism, fall back to the unstructured tetrahedral mesh (true, a diagnostic explains why) or fail (false).
     */
    bool structuredFallback = true;
    /**
     * Gmsh only: limit the element size near thin faces. Each CAD face gets a width estimate 2*area/perimeter
     * (the width of a strip) and the size at its corners is limited to thinFaceSizeFactor times that width.
     * Without it, a flat strip such as a 2.7 mm wide, 1000 mm long flange edge is meshed with needle triangles
     * as long as the maximum element size. A value of 1 is a good start; 0 disables the control. Applies to
     * the faces of unstructured and structured meshes; minElementSize is still respected.
     */
    double thinFaceSizeFactor = 0.0;

    /** Continuity of the mesh across contacts between solids (backends that cannot honour it ignore it and say so). */
    InterfaceMode interfaceMode = InterfaceMode::Independent;
    /**
     * Turn the assembly components, layers and named shapes stored in the CAD file into physical groups
     * (dimension 3 for components, 2 for face groups). Needs a CADModel that reads that structure.
     */
    bool importCadGroups = true;
    /**
     * Gmsh only: map every meshed surface to its CAD face after meshing. This audit is what lets a CAD assembly
     * report its component face structure, but it probes every element of every surface and can take minutes on
     * large models. When false the mapping runs only if there are CAD face groups to build (native dimension-2
     * groups or additionalFaceGroups); without them it is skipped. Faces can then be mapped on demand on the
     * finished mesh with mapCADFacesToMesh().
     */
    bool auditAllCadFaces = true;

    /**
     * Additional user-defined CAD face groups. Backends transfer their boundary
     * element and node memberships into MeshData::MeshGroupRegistry. Gmsh also
     * exposes them as physical groups; TetGen uses facet markers and Netgen uses
     * geometric face queries. The workbench derives solver node sets from the
     * generic registry, never from backend-specific markers.
     *
     * CAD groups imported from the source may also have dimension 0 (vertex)
     * and 1 (edge). Adapters without explicit lower-dimensional elements retain
     * those memberships as node-only groups.
     */
    std::vector<CADGroup> additionalFaceGroups;

    /**
     * Gmsh only: record the verbose (debug level) log of the run, with an OpenModel trace line at the start of
     * each phase (import, 2D/3D meshing, CAD-face mapping with its progress) and its elapsed time. The log is
     * also echoed to the terminal when there is one, and is kept after a failed run.
     */
    bool verboseLog = false;
    /** Gmsh only: file the verbose log is written to when verboseLog is set; empty keeps it in memory only. */
    std::string logFile;
};

class Mesher {
public:
    virtual ~Mesher() = default;

    // Metodo principale: prende il CADModel e popola il MeshData in output
    virtual bool buildMesh(const std::shared_ptr<CADModel>& cadModel, 
                           const MeshingOptions& options, 
                           MeshData& outMesh) = 0;

    virtual std::string getName() const = 0;

    /** @brief Notes and warnings from the last buildMesh call (conformity, group matching). */
    virtual const std::vector<std::string>& diagnostics() const {
        static const std::vector<std::string> none;
        return none;
    }
};