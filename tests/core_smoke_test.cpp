#include <OpenModel/MeshData.hpp>
#include <OpenModel/MeshBase.hpp>
#include <OpenModel/Mesher.hpp>

#include <cassert>
#include <stdexcept>
#include <vector>

int main() {
    MeshData mesh;
    assert(mesh.isEmpty());
    assert(mesh.getNumNodes() == 0);
    assert(mesh.getNumElements() == 0);
    assert(mesh.getNumBlocks() == 0);

    // External IDs are deliberately non-consecutive; internal indices remain compact.
    const auto n0 = mesh.addNode(100, 0.0, 0.0, 0.0);
    const auto n1 = mesh.addNode(350, 1.0, 0.0, 0.0);
    const auto n2 = mesh.addNode(900, 0.0, 1.0, 0.0);
    const auto n3 = mesh.addNode(1200, 0.0, 0.0, 1.0);
    assert(n0 == 0 && n1 == 1 && n2 == 2 && n3 == 3);
    assert(mesh.nodeIndex(900) == 2);
    assert(mesh.nodeId(3) == 1200);
    assert(mesh.nodeCoordinates(1)[0] == 1.0);

    mesh.addElement("Volume", ElementType::Tetrahedron4, 77, {n0, n1, n2, n3});
    mesh.addElement("Volume", ElementType::Tetrahedron4, 125, {n0, n1, n2, n3});
    mesh.addElement("Surface", ElementType::Triangle3, 901, {n0, n1, n2});

    assert(!mesh.isEmpty());
    assert(mesh.getNumNodes() == 4);
    assert(mesh.getNumElements() == 3);
    assert(mesh.getNumBlocks() == 2);
    const auto& volume = mesh.getElementBlock("Volume");
    assert(volume.type == ElementType::Tetrahedron4);
    assert(volume.nodesPerElement == 4);
    assert(volume.ids.size() == 2);
    assert(volume.ids[0] == 77 && volume.ids[1] == 125);
    assert(volume.connectivity.size() == 8);
    assert(volume.connectivity[4] == 0);
    assert(mesh.validate().empty());

    bool duplicateRejected = false;
    try { mesh.addNode(100, 2.0, 2.0, 2.0); }
    catch (const std::invalid_argument&) { duplicateRejected = true; }
    assert(duplicateRejected);

    bool badConnectivityRejected = false;
    try { mesh.addElement("Volume", ElementType::Tetrahedron4, 999, {0, 1, 2, 99}); }
    catch (const std::out_of_range&) { badConnectivityRejected = true; }
    assert(badConnectivityRejected);

    bool blockTopologyRejected = false;
    try { mesh.getOrCreateBlock("Volume", ElementType::Tetrahedron10); }
    catch (const std::invalid_argument&) { blockTopologyRejected = true; }
    assert(blockTopologyRejected);

    mesh.clear();
    assert(mesh.isEmpty());
    assert(mesh.getNumElements() == 0);
    assert(mesh.getNumBlocks() == 0);
    assert(mesh.validate().empty());
}
