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
     * Hexahedral or prismatic mesh obtained by sweeping the mesh of one end face through the solid in
     * layers. Possible only for straight prismatic solids (two congruent parallel planar end faces joined by
     * walls parallel to the sweep direction, e.g. beams and bars); see MeshingOptions::structuredFallback.
     */
    Structured
};

/** @brief Element family of a structured volume mesh. */
enum class StructuredElementShape {
    /** Quadrilateral end-face mesh swept into hexahedra; triangles left by the recombination become prisms. */
    Hexahedron,
    /** Triangular end-face mesh swept into prisms (wedges). */
    Prism
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
     * Gmsh only: volume mesh topology. Structured meshes sweep the end-face mesh in layers of about
     * maxElementSize (so they have far fewer elements than a tetrahedral mesh of the same size), keep the
     * order of the nodes of second-order hexahedra and prisms (20 and 15 nodes, no pyramids) and are not
     * available together with InterfaceMode::Conformal. Ignored for surface meshes.
     */
    MeshStructure structure = MeshStructure::Unstructured;
    /** Gmsh only: element family of a structured volume mesh. */
    StructuredElementShape structuredShape = StructuredElementShape::Hexahedron;
    /**
     * Gmsh only: when a structured mesh is requested but a solid is not a straight prism, fall back to the
     * unstructured tetrahedral mesh (true, a diagnostic explains why) or fail (false).
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