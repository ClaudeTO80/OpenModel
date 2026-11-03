#pragma once

#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>
#include <OpenModel/MeshData.hpp>

#include <string>
#include <vector>

struct AssimpPatchGroupDefinition {
    std::string name;
    std::vector<std::size_t> patchIndices;
    std::string materialName;
};

// Converts mesh-derived patch references into FEM element/node groups.
// Patch indices refer to the ordered output of AssimpSurfacePatchAnalyzer.
class AssimpPatchGroupBuilder {
public:
    static bool apply(const std::vector<RenderSubMesh>& parts,
                      const std::vector<AssimpSurfacePatch>& patches,
                      const std::vector<AssimpPatchGroupDefinition>& definitions,
                      MeshData& mesh,
                      std::vector<std::string>& diagnostics);
};
