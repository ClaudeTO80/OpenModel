#pragma once

#include <OpenModel/CADModel.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

enum class AssimpSurfacePatchKind { Planar, NonPlanar };

// A connected component of imported triangles. Indices refer to the source
// RenderSubMesh and its triangle order; they are not CAD face identifiers.
struct AssimpSurfacePatch {
    std::size_t partIndex = 0;
    std::vector<std::size_t> triangleIndices;
    double area = 0.0;
    std::array<double, 3> centroid{{0.0, 0.0, 0.0}};
    std::array<double, 3> meanNormal{{0.0, 0.0, 0.0}};
    std::array<double, 3> boundsMin{{0.0, 0.0, 0.0}};
    std::array<double, 3> boundsMax{{0.0, 0.0, 0.0}};
    double maxPlanarityDeviation = 0.0;
    double relativePlanarityDeviation = 0.0;
    double normalCoherence = 0.0;
    AssimpSurfacePatchKind kind = AssimpSurfacePatchKind::NonPlanar;
    std::size_t boundaryEdgeCount = 0;
};

// Finds edge-connected patches in an Assimp triangle mesh. Geometric vertex
// welding is used only for adjacency; source vertices and mesh connectivity
// are never modified. Coordinates and weldTolerance use model units.
class AssimpSurfacePatchAnalyzer {
public:
    static bool analyze(const std::vector<RenderSubMesh>& parts,
                        double weldTolerance,
                        std::vector<AssimpSurfacePatch>& patches,
                        std::vector<std::string>& diagnostics,
                        double maxDihedralAngleDegrees = 45.0);
};
