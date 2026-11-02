#pragma once

#include <OpenModel/SurfaceMesh.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct SurfaceRefineOptions {
    double maxEdgeLength = 0.0;             // 0 disables edge-length criterion
    double maxNormalAngleDegrees = 0.0;     // 0 disables normal-angle criterion
    unsigned int maxLevels = 4;
};

struct SurfaceRefineReport {
    bool success = false;
    std::string error;
    std::size_t parts = 0;
    std::size_t verticesBefore = 0, verticesAfter = 0;
    std::size_t trianglesBefore = 0, trianglesAfter = 0;
    unsigned int levelsApplied = 0;
    bool limitReached = false;
};

// Local conforming edge bisection: each edge is split once globally per part
// and every incident triangle consumes that midpoint, avoiding T-junctions.
class SurfaceRefiner {
    using Vec3 = std::array<double,3>;
    using Edge = std::pair<unsigned int,unsigned int>;
    static Vec3 read3(const std::vector<float>& v, std::size_t i) ;
    static Vec3 normalized(Vec3 v) ;
    static double length(const Vec3& a,const Vec3& b) ;
    static std::vector<float> makeNormals(const SurfaceMeshPart& p) ;
    static bool valid(const SurfaceMeshPart& p,std::string& e) ;
    static double angle(const Vec3& a,const Vec3& b);
    static std::map<Edge,bool> edgesToSplit(const SurfaceMeshPart& p,const SurfaceRefineOptions& o) ;
    static void applySplits(SurfaceMeshPart& p,const std::map<Edge,bool>& split) ;
public:
    SurfaceRefineReport refine(SurfaceMesh& mesh,const SurfaceRefineOptions& o) const ;
};
