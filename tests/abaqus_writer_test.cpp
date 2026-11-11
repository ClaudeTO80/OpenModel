#include <OpenModel/AbaqusInpWriter.hpp>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main() {
    const std::string path = "abaqus_writer_test.inp";
    MeshData mesh;
    auto n0 = mesh.addNode(10, 0.0, 0.0, 0.0);
    auto n1 = mesh.addNode(40, 1.0, 0.0, 0.0);
    auto n2 = mesh.addNode(90, 0.0, 1.0, 0.0);
    auto n3 = mesh.addNode(200, 0.0, 0.0, 1.0);
    mesh.addElement("solid region", ElementType::Tetrahedron4, 501, {n0,n1,n2,n3});
    mesh.addElement("solid region", ElementType::Tetrahedron4, 777, {n0,n1,n2,n3});
    mesh.addElement("outer surface", ElementType::Triangle3, 901, {n0,n1,n2});
    mesh.addNodeSet("fixed support", {n0, n2});

    auto result = AbaqusInpWriter::writeFile(path, mesh);
    assert(result.success);
    assert(result.nodesWritten == 4);
    assert(result.elementsWritten == 3);
    assert(result.elementSetsWritten == 2);
    assert(result.nodeSetsWritten == 1);

    std::ifstream in(path, std::ios::binary);
    assert(in.good());
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    assert(text.find("*NODE") != std::string::npos);
    assert(text.find("*NSET, NSET=FIXED_SUPPORT") != std::string::npos);
    assert(text.find("10, 90") != std::string::npos);
    assert(text.find("10, 0, 0, 0") != std::string::npos);
    assert(text.find("*ELEMENT, TYPE=C3D4, ELSET=SOLID_REGION") != std::string::npos);
    assert(text.find("501, 10, 40, 90, 200") != std::string::npos);
    assert(text.find("*ELEMENT, TYPE=S3, ELSET=OUTER_SURFACE") != std::string::npos);
    std::remove(path.c_str());

    // Generic CAD groups are exported independently of mesher-specific physical groups.
    MeshData grouped;
    // The edge set and node membership must survive as *ELSET and *NSET.
    const auto ga = grouped.addNode(11, 0, 0, 0);
    const auto gb = grouped.addNode(12, 1, 0, 0);
    grouped.addElement("cad edge elements", ElementType::Line2, 701, {ga, gb});
    grouped.addMeshGroup({"Edge_A", 1, MeshGroupSource::CAD, {4}, {701}, {ga, gb}});
    grouped.addMeshGroup({"Vertex_A", 0, MeshGroupSource::CAD, {9}, {}, {gb}});
    result = AbaqusInpWriter::writeFile(path, grouped);
    assert(result.success);
    assert(result.elementSetsWritten == 2); // element block + generic CAD edge group
    assert(result.nodeSetsWritten == 2);    // edge and vertex CAD node memberships
    {
        std::ifstream groupsIn(path, std::ios::binary);
        const std::string groupsText((std::istreambuf_iterator<char>(groupsIn)), std::istreambuf_iterator<char>());
        assert(groupsText.find("*ELSET, ELSET=EDGE_A") != std::string::npos);
        assert(groupsText.find("*NSET, NSET=EDGE_A_NODES") != std::string::npos);
        assert(groupsText.find("*NSET, NSET=VERTEX_A_NODES") != std::string::npos);
    }
    std::remove(path.c_str());

    MeshData bad;
    n0 = bad.addNode(1, 0, 0, 0); n1 = bad.addNode(2, 1, 0, 0);
    n2 = bad.addNode(3, 0, 1, 0); n3 = bad.addNode(4, 0, 0, 1);
    bad.addElement("v1", ElementType::Tetrahedron4, 7, {n0,n1,n2,n3});
    bad.addElement("v2", ElementType::Triangle3, 7, {n0,n1,n2});
    auto badResult = AbaqusInpWriter::writeFile(path, bad);
    assert(!badResult.success);
    assert(!badResult.errors.empty());
    std::remove(path.c_str());
    return 0;
}
