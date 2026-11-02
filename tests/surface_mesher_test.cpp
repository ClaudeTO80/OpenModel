#include <OpenModel/SurfaceMesh.hpp>
#include <cassert>
#include <memory>

class MockCAD final : public CADModel {
public:
    std::vector<RenderSubMesh> meshes;
    double lastLinear = 0.0, lastAngular = 0.0;
    std::string path;
    bool loadFromFile(const std::string& p) override { path = p; return true; }
    void tessellate(double linear) override { lastLinear = linear; }
    void tessellate(double linear, double angular) override { lastLinear = linear; lastAngular = angular; }
    const std::vector<RenderSubMesh>& getRenderMeshes() const override { return meshes; }
    std::string getFilePath() const override { return path; }
};

int main() {
    auto cad = std::make_shared<MockCAD>();
    RenderSubMesh part;
    part.name = "PawnBody";
    part.vertices = {0,0,0, 1,0,0, 0,1,0};
    part.normals = {0,0,1, 0,0,1, 0,0,1};
    part.texcoords = {0,0, 1,0, 0,1};
    part.baseColorFactor = {0.1f, 0.2f, 0.3f, 1.0f};
    part.metallicFactor = 0.4f;
    part.roughnessFactor = 0.6f;
    part.baseColorImageMimeType = "image/png";
    part.baseColorImageName = "body.png";
    part.baseColorImage = {137,80,78,71,13,10,26,10};
    part.indices = {0,1,2};
    cad->meshes.push_back(part);

    MeshingOptions opts;
    opts.dimension = MeshDimension::Surface;
    opts.linearDeflection = 0.025;
    opts.angularDeflection = 0.2;
    SurfaceMesh mesh;
    const auto report = SurfaceMesher{}.build(cad, opts, mesh);
    assert(report.ok && report.parts == 1 && report.vertices == 3 && report.triangles == 1);
    assert(cad->lastLinear == 0.025 && cad->lastAngular == 0.2);
    assert(mesh.parts[0].name == "PawnBody");
    assert(mesh.parts[0].texcoords.size() == 6);
    assert(mesh.parts[0].baseColorFactor[1] == 0.2f);
    assert(mesh.parts[0].metallicFactor == 0.4f && mesh.parts[0].roughnessFactor == 0.6f);
    assert(mesh.parts[0].baseColorImageName == "body.png" && mesh.parts[0].baseColorImage.size() == 8);

    cad->meshes[0].indices = {0,1,3};
    const auto bad = SurfaceMesher{}.build(cad, opts, mesh);
    assert(!bad.ok && mesh.empty());

    opts.linearDeflection = 0.0;
    const auto invalid = SurfaceMesher{}.build(cad, opts, mesh);
    assert(!invalid.ok);
}
