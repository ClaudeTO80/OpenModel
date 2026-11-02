#include <OpenModel/SurfaceRefiner.hpp>
#include <cassert>
#include <cmath>

int main() {
    SurfaceMesh mesh;
    SurfaceMeshPart p;
    p.name = "triangle";
    p.positions = {0,0,0, 1,0,0, 0,1,0};
    p.indices = {0,1,2};
    p.texcoords = {0,0, 1,0, 0,1};
    mesh.parts.push_back(p);

    SurfaceRefineOptions opts;
    opts.maxEdgeLength = 0.8;
    opts.maxLevels = 3;
    const auto report = SurfaceRefiner{}.refine(mesh, opts);
    assert(report.success);
    assert(report.verticesBefore == 3 && report.verticesAfter == 6);
    assert(report.trianglesBefore == 1 && report.trianglesAfter == 4);
    assert(!report.limitReached);
    assert(mesh.parts[0].normals.size() == mesh.parts[0].positions.size());
    assert(mesh.parts[0].texcoords.size() == (mesh.parts[0].positions.size()/3)*2);

    // A strict bound requiring another level should be reported honestly when capped.
    SurfaceMesh capped;
    SurfaceMeshPart q;
    q.name = "capped";
    q.positions = {0,0,0, 10,0,0, 0,10,0};
    q.indices = {0,1,2};
    capped.parts.push_back(q);
    opts.maxEdgeLength = 0.01;
    opts.maxLevels = 1;
    const auto cappedReport = SurfaceRefiner{}.refine(capped, opts);
    assert(cappedReport.success && cappedReport.limitReached);
    assert(cappedReport.trianglesAfter == 4);


    // Local refinement splits a shared edge in both incident triangles and preserves UVs.
    SurfaceMesh adjacent; SurfaceMeshPart a; a.name="adjacent";
    a.positions={0,0,0, 2,0,0, 0,1,0, 2,1,0};
    a.indices={0,1,2, 1,3,2}; a.texcoords={0,0, 1,0, 0,1, 1,1};
    adjacent.parts.push_back(a);
    SurfaceRefineOptions local; local.maxEdgeLength=1.1; local.maxLevels=1;
    auto localReport=SurfaceRefiner{}.refine(adjacent,local);
    assert(localReport.success && localReport.verticesAfter>4);
    assert(adjacent.parts[0].texcoords.size()==adjacent.parts[0].positions.size()/3*2);
    assert(adjacent.parts[0].indices.size()%3==0);

    const auto before = mesh.triangleCount();
    opts.maxEdgeLength = -1.0;
    const auto invalid = SurfaceRefiner{}.refine(mesh, opts);
    assert(!invalid.success && mesh.triangleCount() == before);

    opts.maxEdgeLength = 0.0;
    opts.maxNormalAngleDegrees = 0.0;
    const auto noCriterion = SurfaceRefiner{}.refine(mesh, opts);
    assert(!noCriterion.success);
}
