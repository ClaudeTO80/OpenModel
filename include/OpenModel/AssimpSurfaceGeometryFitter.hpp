#pragma once

#include <OpenModel/AssimpSurfacePatchAnalyzer.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

enum class AssimpFittedSurfaceType {
    Planar,
    Cylindrical,
    Spherical,
    Conical,
    Freeform
};

struct AssimpSurfaceFitOptions {
    double maxRelativeRmsError = 1e-3;
    double maxRelativeMaxError = 5e-3;
    double minAngularCoverageDegrees = 45.0;
    double minAxisExtentRatio = 0.05;
};

struct AssimpSurfaceFit {
    std::size_t patchIndex = 0;
    AssimpFittedSurfaceType type = AssimpFittedSurfaceType::Freeform;
    std::array<double, 3> center{{0.0, 0.0, 0.0}};
    std::array<double, 3> axis{{0.0, 0.0, 1.0}};
    double radius = 0.0;
    double coneHalfAngleRadians = 0.0;
    double rmsError = 0.0;
    double maxError = 0.0;
    double relativeRmsError = 0.0;
    double relativeMaxError = 0.0;
    double angularCoverageDegrees = 0.0;
    std::size_t sampleCount = 0;
};

// Fits simple analytic primitives to mesh-derived patches. Results are
// geometric approximations only; they never create CAD B-Rep identities.
class AssimpSurfaceGeometryFitter {
public:
    static bool fit(const std::vector<RenderSubMesh>& parts,
                    const std::vector<AssimpSurfacePatch>& patches,
                    const AssimpSurfaceFitOptions& options,
                    std::vector<AssimpSurfaceFit>& results,
                    std::vector<std::string>& diagnostics);
};
