#include <OpenModel/OCCModel.hpp>

#include <BRepPrimAPI_MakeBox.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Shape.hxx>

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>

int main() {
    const std::string path = "openmodel_occ_integration_box.step";
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(10.0, 20.0, 30.0).Shape();

    STEPControl_Writer writer;
    if (writer.Transfer(box, STEPControl_AsIs) != IFSelect_RetDone ||
        writer.Write(path.c_str()) != IFSelect_RetDone) {
        std::cerr << "Could not create STEP fixture\n";
        return 1;
    }

    OCCModel model;
    if (!model.loadFromFile(path)) {
        std::cerr << "STEP import failed: " << model.getLastError() << '\n';
        std::remove(path.c_str());
        return 2;
    }
    if (!model.hasShape()) {
        std::cerr << "Imported shape is unexpectedly null\n";
        std::remove(path.c_str());
        return 3;
    }
    const auto edgeHits = model.edgesContainingPoint({{5.0, 0.0, 0.0}}, 1e-6);
    const auto vertexHits = model.verticesContainingPoint({{0.0, 0.0, 0.0}}, 1e-6);
    if (edgeHits.empty() || vertexHits.empty()) {
        std::cerr << "CAD edge/vertex geometric queries returned no match\n";
        std::remove(path.c_str());
        return 10;
    }
    if (!model.edgesContainingPoint({{5.0, 0.0, 1.0}}, 1e-6).empty() ||
        !model.verticesContainingPoint({{0.0, 0.0, 1.0}}, 1e-6).empty()) {
        std::cerr << "CAD edge/vertex query accepted a point outside tolerance\n";
        std::remove(path.c_str());
        return 11;
    }

    const auto edgeHits = model.edgesContainingPoint({{5.0, 0.0, 0.0}}, 1e-6);
    const auto vertexHits = model.verticesContainingPoint({{0.0, 0.0, 0.0}}, 1e-6);
    if (edgeHits.empty() || vertexHits.empty()) {
        std::cerr << "CAD edge/vertex geometric queries returned no match\n";
        std::remove(path.c_str());
        return 10;
    }
    if (!model.edgesContainingPoint({{5.0, 0.0, 1.0}}, 1e-6).empty() ||
        !model.verticesContainingPoint({{0.0, 0.0, 1.0}}, 1e-6).empty()) {
        std::cerr << "CAD edge/vertex query accepted a point outside tolerance\n";
        std::remove(path.c_str());
        return 11;
    }

    model.tessellate(0.25);
    std::remove(path.c_str());
    if (model.getRenderMeshes().empty()) {
        std::cerr << "Tessellation failed: " << model.getLastError() << '\n';
        return 4;
    }

    std::size_t vertexCount = 0;
    std::size_t triangleCount = 0;
    for (const auto& sub : model.getRenderMeshes()) {
        if (sub.vertices.size() % 3 != 0 || sub.normals.size() != sub.vertices.size() ||
            sub.indices.size() % 3 != 0) {
            std::cerr << "Malformed render submesh arrays\n";
            return 5;
        }
        const std::size_t n = sub.vertices.size() / 3;
        for (const auto index : sub.indices) {
            if (index >= n) {
                std::cerr << "Triangle index outside vertex range\n";
                return 6;
            }
        }
        for (float value : sub.vertices) {
            if (!std::isfinite(value)) {
                std::cerr << "Non-finite vertex coordinate\n";
                return 7;
            }
        }
        for (float value : sub.normals) {
            if (!std::isfinite(value)) {
                std::cerr << "Non-finite normal\n";
                return 8;
            }
        }
        vertexCount += n;
        triangleCount += sub.indices.size() / 3;
    }

    if (vertexCount == 0 || triangleCount == 0) {
        std::cerr << "Expected non-empty triangulation\n";
        return 9;
    }

    std::cout << "OCC integration test passed: " << model.getRenderMeshes().size()
              << " face submeshes, " << vertexCount << " vertices, "
              << triangleCount << " triangles\n";
    return 0;
}
