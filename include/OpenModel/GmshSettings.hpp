#pragma once

#include <OpenModel/Mesher.hpp>

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>

/**
 * @brief Apply the Gmsh settings of an unstructured (tetrahedral) volume mesh from the request fields.
 *
 * The fields are the keys of the generate-volume request (see src/webui/main.cpp). A key that is absent or empty
 * keeps the value already in `options`, so the defaults of MeshingOptions and TetrahedralGmshOptions (which are
 * Gmsh's own) apply unless a setting is given. A value that is present but malformed is refused with the key named:
 * numbers must be finite and span the whole value, integers must have no fraction, booleans are `0` or `1`.
 *
 * Keys read here: thinF, cadF, cadSmall (CAD sizing), minSize, curvature, curvaturePoints, auditCadFaces,
 * alg2D, alg3D, optimize, optimizeNetgen, optimizeThreshold, smoothing, extendBoundary. The key bodyDims is returned
 * as text because the body dimensions belong to the CAD model: the caller parses them.
 * @param fields the request fields.
 * @param options the options to update.
 * @param bodyDimensions receives the value of bodyDims (empty when absent).
 * @return an empty string on success, otherwise the reason the settings were refused.
 */
inline std::string applyUnstructuredGmshSettings(const std::map<std::string, std::string>& fields, MeshingOptions& options,
                                                 std::string& bodyDimensions) {
    // Reads a finite number that spans the whole value.
    const auto number = [](const std::string& value, double& out) {
        errno = 0;
        char* last = nullptr;
        out = std::strtod(value.c_str(), &last);
        return errno == 0 && last == value.c_str() + value.size() && std::isfinite(out);
    };
    const auto boolean = [](const std::string& value, bool& out) {
        if (value != "0" && value != "1") return false;
        out = value == "1";
        return true;
    };
    const auto integer = [&](const std::string& value, int& out) {
        double parsed = 0.0;
        if (!number(value, parsed) || parsed != std::floor(parsed) || std::fabs(parsed) > 1.0e6) return false;
        out = static_cast<int>(parsed);
        return true;
    };

    std::string error;
    const auto get = [&](const char* key) -> std::string {
        const auto it = fields.find(key);
        return it == fields.end() ? std::string() : it->second;
    };
    const auto setNumber = [&](const char* key, double& target) {
        const std::string value = get(key);
        if (value.empty()) return;
        double parsed = 0.0;
        if (number(value, parsed)) target = parsed; else error = std::string("Gmsh setting '") + key + "' must be a number";
    };
    const auto setBoolean = [&](const char* key, bool& target) {
        const std::string value = get(key);
        if (value.empty()) return;
        bool parsed = false;
        if (boolean(value, parsed)) target = parsed; else error = std::string("Gmsh setting '") + key + "' must be 0 or 1";
    };
    const auto setInteger = [&](const char* key, int& target) {
        const std::string value = get(key);
        if (value.empty()) return;
        int parsed = 0;
        if (integer(value, parsed)) target = parsed; else error = std::string("Gmsh setting '") + key + "' must be an integer";
    };

    setNumber("thinF", options.thinFaceSizeFactor);
    setNumber("cadF", options.cadFaceSizeFactor);
    setNumber("cadSmall", options.cadSmallFaceLength);
    setNumber("minSize", options.minElementSize);
    setBoolean("curvature", options.curvatureAdaptation);
    setNumber("curvaturePoints", options.curvaturePointsPerCircle);
    setBoolean("auditCadFaces", options.auditAllCadFaces);
    setInteger("alg2D", options.tetrahedralGmsh.algorithm2D);
    setInteger("alg3D", options.tetrahedralGmsh.algorithm3D);
    setBoolean("optimize", options.tetrahedralGmsh.optimize);
    setBoolean("optimizeNetgen", options.tetrahedralGmsh.optimizeNetgen);
    setNumber("optimizeThreshold", options.tetrahedralGmsh.optimizeThreshold);
    setInteger("smoothing", options.tetrahedralGmsh.smoothingSteps);
    setBoolean("extendBoundary", options.tetrahedralGmsh.extendSizeFromBoundary);
    if (!error.empty()) return error;

    bodyDimensions = get("bodyDims");
    return {};
}
