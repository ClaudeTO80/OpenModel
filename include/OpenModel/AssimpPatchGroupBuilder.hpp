#pragma once

#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>
#include <OpenModel/MeshData.hpp>

#include <array>
#include <limits>
#include <string>
#include <vector>

struct AssimpPatchFilter {
    bool planarOnly = false;
    double minArea = 0.0;
    double maxArea = std::numeric_limits<double>::infinity();
    double maxPlanarityDeviation = std::numeric_limits<double>::infinity();
    double minNormalCoherence = 0.0;
    bool constrainCentroid = false;
    std::array<double, 3> centroidMin{{-std::numeric_limits<double>::max(),
        -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max()}};
    std::array<double, 3> centroidMax{{std::numeric_limits<double>::max(),
        std::numeric_limits<double>::max(), std::numeric_limits<double>::max()}};
    bool constrainNormal = false;
    std::array<double, 3> direction{{0.0, 0.0, 1.0}};
    double minNormalAlignment = 0.95;
};

struct AssimpPatchGroupDefinition {
    std::string name;
    std::vector<std::size_t> patchIndices;
    std::string materialName;
    bool selectByFilter = false;
    AssimpPatchFilter filter;
};

// Converts mesh-derived patch references into FEM element/node groups.
// Patch indices refer to the ordered output of AssimpSurfacePatchAnalyzer.
// Alternatively, selectByFilter resolves patches from their measured geometry.
class AssimpPatchGroupBuilder {
public:
    static bool apply(const std::vector<RenderSubMesh>& parts,
                      const std::vector<AssimpSurfacePatch>& patches,
                      const std::vector<AssimpPatchGroupDefinition>& definitions,
                      MeshData& mesh,
                      std::vector<std::string>& diagnostics);
};
