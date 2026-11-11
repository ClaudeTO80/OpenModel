#include <OpenModel/AssimpModel.hpp>
#include <OpenModel/AssimpSurfaceMesher.hpp>
#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>
#include <OpenModel/AssimpPatchGroupBuilder.hpp>
#include <OpenModel/AssimpSurfaceGeometryFitter.hpp>

int main() {
    AssimpModel model;
    AssimpSurfaceMesher mesher;
    return mesher.getName().empty() ? 1 : 0;
}
