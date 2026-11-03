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

struct MeshingOptions {
    MeshDimension dimension = MeshDimension::Volume;
    // Legacy volume target size; retained for source compatibility.
    double elementSize = 1.0;
    int order = 1; // 1 = linear, 2 = quadratic (backend dependent)
    bool generateTetrahedra = true;

    // CAD tessellation controls. linearDeflection is a geometric chordal
    // tolerance, not a guaranteed triangle edge length.
    double linearDeflection = 0.1;
    double angularDeflection = 0.5; // radians for Open CASCADE
    double minElementSize = 0.0;   // 0 = backend default / no explicit minimum
    double maxElementSize = 0.0;   // 0 = use elementSize as the maximum
    bool curvatureAdaptation = true;
    double curvaturePointsPerCircle = 12.0; // hint for backends that support it

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
     */
    std::vector<CADGroup> additionalFaceGroups;
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