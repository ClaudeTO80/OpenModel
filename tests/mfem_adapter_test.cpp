#include <OpenModel/MFEMMesh.hpp>

#include <cassert>
#include <iostream>

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

    MFEMMesh adapted;
    assert(adapted.createFromData(mesh, 3));
    assert(adapted.getNativeMesh() != nullptr);
    assert(adapted.getNumNodes() == 4);
    assert(adapted.getNumElements() == 1);
    assert(adapted.getNativeMesh()->GetNBE() == 1);
    assert(adapted.getNativeMesh()->GetElement(0)->GetAttribute() == 7);
    assert(adapted.getNativeMesh()->GetBdrElement(0)->GetAttribute() == 11);

    MeshData surface;
    const auto a = surface.addNode(1, 0.0, 0.0, 0.0);
    const auto b = surface.addNode(2, 1.0, 0.0, 0.0);
    const auto c = surface.addNode(3, 1.0, 1.0, 0.0);
    const auto d = surface.addNode(4, 0.0, 1.0, 0.0);
    surface.addElement("Surface", ElementType::Quadrilateral4, 10, {a, b, c, d});
    assert(adapted.createFromData(surface, 2));
    assert(adapted.getNumNodes() == 4);
    assert(adapted.getNumElements() == 1);
    assert(adapted.getNativeMesh()->Dimension() == 2);

    MeshData unsupported;
    const auto u0 = unsupported.addNode(0.0, 0.0, 0.0);
    const auto u1 = unsupported.addNode(1.0, 0.0, 0.0);
    const auto u2 = unsupported.addNode(0.0, 1.0, 0.0);
    unsupported.addElement("Quadratic", ElementType::Triangle6, 1, {u0, u1, u2, u0, u1, u2});
    assert(!adapted.createFromData(unsupported, 2));
    assert(!adapted.getLastError().empty());

    std::cout << "MFEM adapter tests passed\n";
    return 0;
}
