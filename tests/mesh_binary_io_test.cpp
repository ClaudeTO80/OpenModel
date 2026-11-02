#include <OpenModel/MeshBinaryIO.hpp>

#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main() {
    const auto root = std::filesystem::temp_directory_path();
    const auto path = root / "openmodel_binary_io_test.mgmb";
    const auto corrupt = root / "openmodel_binary_io_test_corrupt.mgmb";
    const auto truncated = root / "openmodel_binary_io_test_truncated.mgmb";

    MeshData source;
    const auto a = source.addNode(1001, 0.0, 0.0, 0.0);
    const auto b = source.addNode(77, 1.25, 0.0, 0.0);
    const auto c = source.addNode(800000, 0.0, 2.5, 0.0);
    const auto d = source.addNode(42, 0.0, 0.0, 3.75);
    source.addElement("volume/tets", ElementType::Tetrahedron4, 90001, {a, b, c, d});
    source.addElement("surface triangles", ElementType::Triangle3, 12, {a, b, c});
    source.addNodeSet("fixed nodes", {a, d});
    source.addNodeSet("empty set", {});
    source.addPhysicalGroup("solid", 3, 7, {90001}, "steel");
    source.addPhysicalGroup("outer surface", 2, 8, {12});
    assert(source.validate().empty());

    MeshBinaryIO::write(path, source);
    // Replacing an existing destination must also work.
    MeshBinaryIO::write(path, source);
    const auto loaded = MeshBinaryIO::read(path);
    assert(loaded.getNumNodes() == source.getNumNodes());
    assert(loaded.getNumElements() == source.getNumElements());
    assert(loaded.getNumBlocks() == source.getNumBlocks());
    assert(loaded.nodeId(0) == 1001 && loaded.nodeId(1) == 77 && loaded.nodeId(2) == 800000);
    assert(loaded.nodeIndex(42) == 3);
    const auto coords = loaded.nodeCoordinates(2);
    assert(coords[0] == 0.0 && coords[1] == 2.5 && coords[2] == 0.0);
    const auto& volume = loaded.getElementBlock("volume/tets");
    assert(volume.type == ElementType::Tetrahedron4 && volume.nodesPerElement == 4);
    assert(volume.ids.size() == 1 && volume.ids[0] == 90001);
    assert((volume.connectivity == std::vector<NodeIndex>{0, 1, 2, 3}));
    assert(loaded.getNodeSets().at("fixed nodes") == std::vector<NodeIndex>({0, 3}));
    assert(loaded.getNodeSets().at("empty set").empty());
    assert(loaded.getPhysicalGroups().size() == 2);
    assert(loaded.getPhysicalGroups()[0].name == "solid");
    assert(loaded.getPhysicalGroups()[0].materialName == "steel");
    assert(loaded.validate().empty());

    // Empty meshes are valid round-trip inputs too.
    const auto emptyPath = root / "openmodel_binary_io_empty.mgmb";
    MeshData empty;
    MeshBinaryIO::write(emptyPath, empty);
    assert(MeshBinaryIO::read(emptyPath).isEmpty());

    // Reject a malformed signature and a truncated file.
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
        assert(bytes.size() > 64);
        bytes[0] = 'X';
        std::ofstream out(corrupt, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    bool badMagicRejected = false;
    try { (void)MeshBinaryIO::read(corrupt); } catch (const MeshBinaryIO::Error&) { badMagicRejected = true; }
    assert(badMagicRejected);
    {
        std::ifstream in(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(in)), {});
        bytes.resize(20);
        std::ofstream out(truncated, std::ios::binary | std::ios::trunc);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }
    bool truncatedRejected = false;
    try { (void)MeshBinaryIO::read(truncated); } catch (const MeshBinaryIO::Error&) { truncatedRejected = true; }
    assert(truncatedRejected);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(corrupt, ignored);
    std::filesystem::remove(truncated, ignored);
    std::filesystem::remove(emptyPath, ignored);
    return 0;
}
