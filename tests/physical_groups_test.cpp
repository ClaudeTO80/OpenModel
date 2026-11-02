#include <vector>
#include <chrono>
#include <OpenModel/MeshData.hpp>
#include <OpenModel/AbaqusInpWriter.hpp>

#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main() {
    MeshData mesh;
    const auto n0 = mesh.addNode(10, 0.0, 0.0, 0.0);
    const auto n1 = mesh.addNode(20, 1.0, 0.0, 0.0);
    const auto n2 = mesh.addNode(30, 0.0, 1.0, 0.0);
    const auto n3 = mesh.addNode(40, 0.0, 0.0, 1.0);
    mesh.addElement("Volume", ElementType::Tetrahedron4, 100, {n0, n1, n2, n3});
    mesh.addElement("Boundary", ElementType::Triangle3, 200, {n0, n2, n1});
    mesh.addPhysicalGroup("SteelRegion", 3, 7, {100}, "Steel");
    mesh.addPhysicalGroup("FixedFace", 2, 11, {200});
    assert(mesh.validate().empty());

    const std::string path = "physical_groups_test.inp";
    const auto result = AbaqusInpWriter::writeFile(path, mesh);
    assert(result.success);
    assert(result.elementSetsWritten == 4); // 2 block ELSETs + 2 physical groups
    std::ifstream input(path);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    assert(contents.find("*ELSET, ELSET=STEELREGION") != std::string::npos);
    assert(contents.find("** PHYSICAL_GROUP, DIMENSION=3, TAG=7, MATERIAL=Steel") != std::string::npos);
    assert(contents.find("*ELSET, ELSET=FIXEDFACE") != std::string::npos);
    std::remove(path.c_str());

    MeshData invalid;
    const auto a = invalid.addNode(0, 0, 0);
    const auto b = invalid.addNode(1, 0, 0);
    const auto c = invalid.addNode(0, 1, 0);
    invalid.addElement("Surface", ElementType::Triangle3, 5, {a, b, c});
    invalid.addPhysicalGroup("BadDimension", 3, 1, {5});
    assert(!invalid.validate().empty());

    bool duplicateTagRejected = false;
    try { invalid.addPhysicalGroup("Other", 3, 1, {}); }
    catch (const std::invalid_argument&) { duplicateTagRejected = true; }
    assert(duplicateTagRejected);

    // Duplicate element IDs are dropped, the first occurrence keeps its position,
    // and a very large group is built in linear time (it used to be quadratic).
    MeshData big;
    std::vector<ElementId> ids;
    for (ElementId id = 1; id <= 300000; ++id) ids.push_back(id);
    ids.push_back(7); ids.push_back(300000); ids.insert(ids.begin(), 42);
    const auto started = std::chrono::steady_clock::now();
    big.addPhysicalGroup("Large", 3, 1, ids);
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const auto& stored = big.getPhysicalGroups().front().elementIds;
    assert(stored.size() == 300000);
    assert(stored.front() == 42 && stored[1] == 1 && stored.back() == 300000);
    assert(seconds < 2.0);
    return 0;
}
