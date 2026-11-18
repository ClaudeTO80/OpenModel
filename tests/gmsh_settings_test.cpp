#include <OpenModel/GmshSettings.hpp>

#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

using Fields = std::map<std::string, std::string>;

/** Applies the request fields to fresh options; returns the error (empty on success). */
std::string apply(const Fields& fields, MeshingOptions& options, std::string& bodyDimensions) {
    return applyUnstructuredGmshSettings(fields, options, bodyDimensions);
}
}  // namespace

int main() {
    try {
        // No tetrahedral keys: every option keeps the value it had, so an unset GUI changes nothing.
        {
            MeshingOptions defaults;
            MeshingOptions options;
            std::string bodies;
            require(apply({{"mesher", "gmsh"}, {"size", "4"}}, options, bodies).empty(),
                    "Requests without tetrahedral keys must be accepted");
            require(options.minElementSize == defaults.minElementSize, "Absent minSize must keep the default");
            require(options.curvatureAdaptation == defaults.curvatureAdaptation, "Absent curvature must keep the default");
            require(options.tetrahedralGmsh.algorithm2D == 6 && options.tetrahedralGmsh.algorithm3D == 1,
                    "Absent algorithms must keep Gmsh's defaults");
            require(options.tetrahedralGmsh.optimize && !options.tetrahedralGmsh.optimizeNetgen,
                    "Absent optimisation keys must keep Gmsh's defaults");
            require(bodies.empty(), "Absent bodyDims must give no body dimensions");
        }

        // Empty values count as absent, as in the GUI when a field is cleared.
        {
            MeshingOptions options;
            std::string bodies;
            require(apply({{"minSize", ""}, {"alg3D", ""}}, options, bodies).empty(),
                    "Empty values must be treated as absent");
            require(options.minElementSize == 0.0 && options.tetrahedralGmsh.algorithm3D == 1,
                    "Empty values must keep the defaults");
        }

        // Every key with values different from the defaults.
        {
            MeshingOptions options;
            std::string bodies;
            const Fields fields{
                {"thinF", "1.5"}, {"cadF", "0.25"}, {"cadSmall", "3"}, {"bodyDims", "3,2,3"},
                {"minSize", "0.5"}, {"curvature", "0"}, {"curvaturePoints", "18"}, {"auditCadFaces", "0"},
                {"alg2D", "5"}, {"alg3D", "10"}, {"optimize", "0"}, {"optimizeNetgen", "1"},
                {"optimizeThreshold", "0.6"}, {"smoothing", "4"}, {"extendBoundary", "0"},
            };
            const std::string error = apply(fields, options, bodies);
            require(error.empty(), "Valid settings were refused: " + error);
            require(options.thinFaceSizeFactor == 1.5, "thinF not applied");
            require(options.cadFaceSizeFactor == 0.25, "cadF not applied");
            require(options.cadSmallFaceLength == 3.0, "cadSmall not applied");
            require(bodies == "3,2,3", "bodyDims must be returned as text");
            require(options.minElementSize == 0.5, "minSize not applied");
            require(!options.curvatureAdaptation, "curvature not applied");
            require(options.curvaturePointsPerCircle == 18.0, "curvaturePoints not applied");
            require(!options.auditAllCadFaces, "auditCadFaces not applied");
            require(options.tetrahedralGmsh.algorithm2D == 5, "alg2D not applied");
            require(options.tetrahedralGmsh.algorithm3D == 10, "alg3D not applied");
            require(!options.tetrahedralGmsh.optimize, "optimize not applied");
            require(options.tetrahedralGmsh.optimizeNetgen, "optimizeNetgen not applied");
            require(options.tetrahedralGmsh.optimizeThreshold == 0.6, "optimizeThreshold not applied");
            require(options.tetrahedralGmsh.smoothingSteps == 4, "smoothing not applied");
            require(!options.tetrahedralGmsh.extendSizeFromBoundary, "extendBoundary not applied");
        }

        // Refused values: each message names the key.
        {
            const std::pair<Fields, std::string> cases[] = {
                std::pair<Fields, std::string>{Fields{{"minSize", "nan"}}, "minSize"},
                std::pair<Fields, std::string>{Fields{{"minSize", "0.5x"}}, "minSize"},
                std::pair<Fields, std::string>{Fields{{"curvaturePoints", "many"}}, "curvaturePoints"},
                std::pair<Fields, std::string>{Fields{{"curvature", "2"}}, "curvature"},
                std::pair<Fields, std::string>{Fields{{"optimize", "yes"}}, "optimize"},
                std::pair<Fields, std::string>{Fields{{"alg3D", "2.5"}}, "alg3D"},
                std::pair<Fields, std::string>{Fields{{"smoothing", "1e9"}}, "smoothing"},
                std::pair<Fields, std::string>{Fields{{"thinF", "inf"}}, "thinF"},
            };
            for (const auto& [fields, key] : cases) {
                MeshingOptions options;
                std::string bodies;
                const std::string error = apply(fields, options, bodies);
                require(!error.empty(), "Refused value of '" + key + "' was accepted");
                require(error.find("'" + key + "'") != std::string::npos,
                        "The refusal must name '" + key + "': " + error);
            }
        }

        std::cout << "gmsh_settings_test passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gmsh_settings_test failed: " << error.what() << "\n";
        return EXIT_FAILURE;
    }
}
