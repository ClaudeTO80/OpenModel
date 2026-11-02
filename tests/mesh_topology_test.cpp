#include <OpenModel/MeshTopology.hpp>

#include <cassert>
#include <iostream>

int main() {
    MeshData mesh;
    const auto n0 = mesh.addNode(10, 0, 0, 0);
    const auto n1 = mesh.addNode(20, 1, 0, 0);
    const auto n2 = mesh.addNode(30, 0, 1, 0);
    const auto n3 = mesh.addNode(40, 0, 0, 1);
    const auto n4 = mesh.addNode(50, 0, 0, -1);
    mesh.addElement("Tets", ElementType::Tetrahedron4, 101, {n0,n1,n2,n3});
    mesh.addElement("Tets", ElementType::Tetrahedron4, 205, {n0,n2,n1,n4});
    mesh.addPhysicalGroup("MaterialA", 3, 1, {101}, "Steel");
    mesh.addPhysicalGroup("MaterialB", 3, 2, {205}, "Aluminium");

    MeshTopology topology;
    const auto report = topology.build(mesh);
    assert(report.success());
    assert(report.boundaryFaces == 6);
    assert(report.internalFaces == 1);
    assert(report.nonManifoldFaces == 0);
    assert(report.boundaryEdges == 9);
    assert(report.openBoundaryEdges == 0);
    assert(report.nonManifoldEdges == 0);
    assert(topology.faces().size() == 7);
    assert(topology.edges().size() == 9);

    std::size_t interfaces = 0;
    for (const auto& face : topology.faces())
        if (MeshTopology::isMaterialInterface(face, mesh)) ++interfaces;
    assert(interfaces == 1);

    // Node sets are stable across topology construction and use internal indices.
    mesh.addNodeSet("Support", {n0, n1});
    assert(mesh.getNodeSets().at("Support").size() == 2);
    // Quadratic tetrahedra share a face only when all three corner nodes and
    // all three midside nodes match. Surface physical groups are attached by
    // exact face connectivity, not merely by a coincident corner set.
    MeshData quadratic;
    const auto q0 = quadratic.addNode(1, 0, 0, 0);
    const auto q1 = quadratic.addNode(2, 1, 0, 0);
    const auto q2 = quadratic.addNode(3, 0, 1, 0);
    const auto q3 = quadratic.addNode(4, 0, 0, 1);
    const auto q4 = quadratic.addNode(5, 0, 0, -1);
    const auto m01 = quadratic.addNode(6, .5, 0, 0);
    const auto m12 = quadratic.addNode(7, .5, .5, 0);
    const auto m20 = quadratic.addNode(8, 0, .5, 0);
    const auto m03 = quadratic.addNode(9, 0, 0, .5);
    const auto m13 = quadratic.addNode(10, .5, 0, .5);
    const auto m23 = quadratic.addNode(11, 0, .5, .5);
    const auto m04 = quadratic.addNode(12, 0, 0, -.5);
    const auto m14 = quadratic.addNode(13, .5, 0, -.5);
    const auto m24 = quadratic.addNode(14, 0, .5, -.5);
    quadratic.addElement("Tet10", ElementType::Tetrahedron10, 1001,
        {q0,q1,q2,q3,m01,m12,m20,m03,m13,m23});
    quadratic.addElement("Tet10", ElementType::Tetrahedron10, 1002,
        {q0,q2,q1,q4,m20,m12,m01,m04,m24,m14});
    quadratic.addElement("Surface", ElementType::Triangle6, 2001,
        {q0,q1,q3,m01,m13,m03});
    quadratic.addPhysicalGroup("LoadedSurface", 2, 9, {2001});
    MeshTopology qtop;
    const auto qr = qtop.build(quadratic);
    assert(qr.success());
    assert(qr.internalFaces == 1);
    assert(qr.boundaryFaces == 6);
    bool foundQuadraticInternal = false;
    bool foundTaggedBoundary = false;
    for (const auto& face : qtop.faces()) {
        if (face.classification == MeshFaceClass::Internal) {
            foundQuadraticInternal = true;
            assert(face.type == MeshFaceType::Triangle6);
            assert(face.nodes.size() == 6);
        }
        if (face.physicalGroups.end() != std::find(face.physicalGroups.begin(), face.physicalGroups.end(), "LoadedSurface")) {
            foundTaggedBoundary = true;
            assert(face.classification == MeshFaceClass::Boundary);
            assert(face.type == MeshFaceType::Triangle6);
        }
    }
    assert(foundQuadraticInternal && foundTaggedBoundary);

    // A face shared by three volume elements is explicitly reported as non-manifold.
    MeshData nonManifold;
    const auto a = nonManifold.addNode(1, 0, 0, 0);
    const auto b = nonManifold.addNode(2, 1, 0, 0);
    const auto c = nonManifold.addNode(3, 0, 1, 0);
    const auto d = nonManifold.addNode(4, 0, 0, 1);
    const auto e = nonManifold.addNode(5, 0, 0, -1);
    const auto f = nonManifold.addNode(6, .2, .2, 1);
    nonManifold.addElement("Tets", ElementType::Tetrahedron4, 1, {a,b,c,d});
    nonManifold.addElement("Tets", ElementType::Tetrahedron4, 2, {a,c,b,e});
    nonManifold.addElement("Tets", ElementType::Tetrahedron4, 3, {a,b,c,f});
    MeshTopology ntop;
    const auto nr = ntop.build(nonManifold);
    assert(nr.success());
    assert(nr.nonManifoldFaces == 1);
    assert(!nr.warnings.empty());

    // High-order hexahedron face templates include face-centre nodes for Hex27.
    MeshData hex27;
    std::vector<NodeIndex> hexNodes;
    for (std::size_t i = 0; i < 27; ++i)
        hexNodes.push_back(hex27.addNode(static_cast<NodeId>(i + 1), static_cast<double>(i), 0.0, 0.0));
    hex27.addElement("Hex27", ElementType::Hexahedron27, 301, hexNodes);
    MeshTopology htop;
    const auto hr = htop.build(hex27);
    assert(hr.success());
    assert(htop.faces().size() == 6);
    for (const auto& face : htop.faces()) {
        assert(face.type == MeshFaceType::Quadrilateral9);
        assert(face.nodes.size() == 9);
        assert(face.cornerCount == 4);
    }

    // Prism15 contributes two Tri6 and three Quad8 faces.
    MeshData prism15;
    std::vector<NodeIndex> prismNodes;
    for (std::size_t i = 0; i < 15; ++i)
        prismNodes.push_back(prism15.addNode(static_cast<NodeId>(i + 1), static_cast<double>(i), 1.0, 0.0));
    prism15.addElement("Prism15", ElementType::Prism15, 401, prismNodes);
    MeshTopology ptop;
    const auto pr = ptop.build(prism15);
    assert(pr.success());
    assert(ptop.faces().size() == 5);
    std::size_t tri6 = 0, quad8 = 0;
    for (const auto& face : ptop.faces()) {
        if (face.type == MeshFaceType::Triangle6) ++tri6;
        if (face.type == MeshFaceType::Quadrilateral8) ++quad8;
    }
    assert(tri6 == 2 && quad8 == 3);

    std::cout << "mesh_topology_test passed\n";
}
