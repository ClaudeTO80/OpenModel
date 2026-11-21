#include <OpenModel/MeshBinaryIO.hpp>
#include <OpenModel/ModelBinaryIO.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// Builds a structured hexahedral grid with sequential node IDs (the common case for generated meshes), a physical
// group, a mesh group and a constraint. Used for the size comparison against MGMB.
MeshData makeStructuredGrid(std::size_t n) {
    MeshData mesh;
    const auto side = n + 1;
    std::vector<NodeIndex> nodes(side * side * side);
    for (std::size_t k = 0; k < side; ++k)
        for (std::size_t j = 0; j < side; ++j)
            for (std::size_t i = 0; i < side; ++i) {
                const auto index = (k * side + j) * side + i;
                nodes[index] = mesh.addNode(static_cast<NodeId>(index + 1),
                                            static_cast<double>(i), static_cast<double>(j), static_cast<double>(k));
            }
    std::vector<ElementId> elementIds;
    ElementId id = 1;
    auto at = [&](std::size_t i, std::size_t j, std::size_t k) { return nodes[(k * side + j) * side + i]; };
    for (std::size_t k = 0; k < n; ++k)
        for (std::size_t j = 0; j < n; ++j)
            for (std::size_t i = 0; i < n; ++i) {
                mesh.addElement("solid", ElementType::Hexahedron8, id,
                                {at(i, j, k), at(i + 1, j, k), at(i + 1, j + 1, k), at(i, j + 1, k),
                                 at(i, j, k + 1), at(i + 1, j, k + 1), at(i + 1, j + 1, k + 1), at(i, j + 1, k + 1)});
                elementIds.push_back(id++);
            }
    mesh.addPhysicalGroup("solid", 3, 1, elementIds, "steel");
    mesh.addMeshGroup(MeshGroup{"Fixed face", 2, MeshGroupSource::CAD, {7}, {}, {nodes[0], nodes[side]}});
    mesh.constrainNodes({nodes[0], nodes[side]}, kDofTranslations);
    assert(mesh.validate().empty());
    return mesh;
}

std::uintmax_t fileSize(const std::filesystem::path& path) {
    return std::filesystem::file_size(path);
}

template <class F>
bool throwsModelError(F&& action) {
    try {
        action();
    } catch (const ModelBinaryIO::Error&) {
        return true;
    }
    return false;
}

void writeBytes(const std::filesystem::path& path, const std::vector<char>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::vector<char> readBytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

int main() {
    const auto root = std::filesystem::temp_directory_path();
    const auto path = root / "openmodel_model_io_test.omb";
    const auto corrupt = root / "openmodel_model_io_test_corrupt.omb";
    const auto truncated = root / "openmodel_model_io_test_truncated.omb";
    const auto badMagic = root / "openmodel_model_io_test_magic.omb";

    // 1. Full round trip: every section, non-sequential IDs, both connectivity encodings, bodies and constraints.
    MeshData source;
    const auto a = source.addNode(1001, 0.0, 0.0, 0.0);
    const auto b = source.addNode(77, 1.25, 0.0, 0.0);
    const auto c = source.addNode(800000, 0.0, 2.5, 0.0);
    const auto d = source.addNode(42, 0.0, 0.0, 3.75);
    source.addElement("volume/tets", ElementType::Tetrahedron4, 90001, {a, b, c, d});
    source.addElement("surface triangles", ElementType::Triangle3, 12, {a, b, c});
    // Scattered connectivity: the raw encoding should be selected for this block.
    for (ElementId e = 0; e < 40; ++e)
        source.addElement("scattered", ElementType::Line2, 500 + e * 9973, {d, (e % 2 == 0) ? a : b});
    source.addNodeSet("fixed nodes", {a, d});
    source.addNodeSet("empty set", {});
    source.addPhysicalGroup("solid", 3, 7, {90001}, "steel");
    source.addPhysicalGroup("outer surface", 2, 8, {12});
    source.addMeshGroup(MeshGroup{"CAD_Face_A", 2, MeshGroupSource::CAD, {101, -102}, {12}, {a, b, c}});
    source.addMeshGroup(MeshGroup{"ImportedRegion", 3, MeshGroupSource::Imported, {}, {90001}, {a, b, c, d}});
    source.addMeshGroup(MeshGroup{"CAD_Vertex_A", 0, MeshGroupSource::CAD, {301}, {}, {a}});
    source.constrainNode(a, kDofAll);
    source.constrainNode(d, DofUz, {0.0, 0.0, 0.0, 0.0, 0.0, 0.0});
    source.constrainNode(c, DofUx | DofRz, {0.5, 0.0, 0.0, 0.0, 0.0, -0.25});
    std::vector<MeshBody> bodies;
    bodies.push_back(MeshBody{"krups", 3, {90001}});
    bodies.push_back(MeshBody{"sheet", 2, {12}});
    source.setBodies(bodies);
    assert(source.validate().empty());

    ModelBinaryIO::write(path, source);
    ModelBinaryIO::write(path, source); // replacing an existing file must work
    // No temporary file may be left behind by an atomic write.
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        const auto name = entry.path().filename().string();
        assert(name.find("openmodel_model_io_test.omb.tmp.") != 0);
    }

    const auto loaded = ModelBinaryIO::read(path);
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
    const auto& scattered = loaded.getElementBlock("scattered");
    assert(scattered.ids.size() == 40 && scattered.ids[39] == 500 + 39 * 9973);
    assert(scattered.connectivity.size() == 80);
    // Element 0 is {d, a}: indices {3, 0}.
    assert(scattered.connectivity[0] == 3 && scattered.connectivity[1] == 0);
    assert(loaded.getNodeSets().at("fixed nodes") == std::vector<NodeIndex>({0, 3}));
    assert(loaded.getNodeSets().at("empty set").empty());
    assert(loaded.getPhysicalGroups().size() == 2);
    assert(loaded.getPhysicalGroups()[0].name == "solid");
    assert(loaded.getPhysicalGroups()[0].materialName == "steel");
    assert(loaded.getMeshGroupRegistry().groups().size() == 3);
    const auto* loadedFace = loaded.getMeshGroupRegistry().find("CAD_Face_A");
    assert(loadedFace != nullptr && loadedFace->source == MeshGroupSource::CAD);
    assert((loadedFace->sourceEntityIds == std::vector<int>{101, -102}));
    assert((loadedFace->nodeIndices == std::vector<NodeIndex>{0, 1, 2}));
    assert(loaded.getConstraints().size() == 3);
    const auto* loadedC = loaded.constraintOf(c);
    assert(loadedC != nullptr && loadedC->dofs == (DofUx | DofRz));
    assert(loadedC->values[0] == 0.5 && loadedC->values[5] == -0.25);
    assert(loadedC->values[1] == 0.0 && loadedC->values[2] == 0.0); // free degrees of freedom stay zero
    const auto* loadedD = loaded.constraintOf(d);
    assert(loadedD != nullptr && loadedD->dofs == DofUz);
    assert(loaded.getBodies().size() == 2 && loaded.getBodies()[0].name == "krups");
    assert(loaded.getBodies()[0].elementIds == std::vector<ElementId>({90001}));
    assert(loaded.getBodies()[1].dimension == 2);
    assert(loaded.validate().empty());

    // 2. An empty mesh round-trips.
    const auto emptyPath = root / "openmodel_model_io_test_empty.omb";
    ModelBinaryIO::write(emptyPath, MeshData{});
    const auto emptyLoaded = ModelBinaryIO::read(emptyPath);
    assert(emptyLoaded.isEmpty() && emptyLoaded.getBodies().empty() && emptyLoaded.getConstraints().empty());
    std::filesystem::remove(emptyPath);

    // 3. Size against MGMB on a structured grid with sequential IDs: the reason for the format.
    const auto grid = makeStructuredGrid(24); // 13,824 nodes, 13,824 hexahedra
    const auto mgmbPath = root / "openmodel_model_io_test_grid.mgmb";
    const auto mgomPath = root / "openmodel_model_io_test_grid.omb";
    MeshBinaryIO::write(mgmbPath, grid);
    ModelBinaryIO::write(mgomPath, grid);
    const auto mgmbBytes = fileSize(mgmbPath);
    const auto mgomBytes = fileSize(mgomPath);
    std::printf("grid 24^3: MGMB %ju bytes, MGOM %ju bytes (%.1f%% of MGMB)\n", mgmbBytes, mgomBytes,
                100.0 * static_cast<double>(mgomBytes) / static_cast<double>(mgmbBytes));
    assert(mgomBytes * 2 < mgmbBytes); // at least a 2x reduction on this mesh
    const auto gridLoaded = ModelBinaryIO::read(mgomPath);
    assert(gridLoaded.getNumElements() == grid.getNumElements());
    assert(gridLoaded.getElementBlock("solid").connectivity == grid.getElementBlock("solid").connectivity);
    assert(gridLoaded.getNodeIds() == grid.getNodeIds());
    assert(gridLoaded.getCoordinates() == grid.getCoordinates()); // binary64, bit-exact
    std::filesystem::remove(mgmbPath);
    std::filesystem::remove(mgomPath);

    // 4. Corruption, truncation and foreign files are rejected, never returned as a partial mesh.
    auto bytes = readBytes(path);
    assert(bytes.size() > 64);
    auto damaged = bytes;
    damaged[damaged.size() / 2] ^= 0x40;
    writeBytes(corrupt, damaged);
    assert(throwsModelError([&] { ModelBinaryIO::read(corrupt); }));

    auto cut = bytes;
    cut.resize(bytes.size() - 17);
    writeBytes(truncated, cut);
    assert(throwsModelError([&] { ModelBinaryIO::read(truncated); }));

    // Exhaustive single-bit corruption: every flip must be rejected with ModelBinaryIO::Error. Any other exception
    // type escapes this loop and aborts the test, and an accepted flip fails the assertion.
    for (std::size_t i = 0; i < bytes.size(); ++i)
        for (int bit = 0; bit < 8; ++bit) {
            auto flipped = bytes;
            flipped[i] = static_cast<char>(flipped[i] ^ (1 << bit));
            writeBytes(corrupt, flipped);
            bool rejected = false;
            try {
                ModelBinaryIO::read(corrupt);
            } catch (const ModelBinaryIO::Error&) {
                rejected = true;
            }
            assert(rejected);
        }

    auto foreign = bytes;
    foreign[0] = 'X';
    writeBytes(badMagic, foreign);
    assert(throwsModelError([&] { ModelBinaryIO::read(badMagic); }));

    // An MGMB file is not an MGOM file.
    const auto mgmbSmall = root / "openmodel_model_io_test_small.mgmb";
    MeshBinaryIO::write(mgmbSmall, source);
    assert(throwsModelError([&] { ModelBinaryIO::read(mgmbSmall); }));
    std::filesystem::remove(mgmbSmall);

    // 5. Writing a structurally invalid mesh is refused and leaves no file behind.
    MeshData invalid;
    invalid.addNode(1, 0.0, 0.0, 0.0);
    invalid.addPhysicalGroup("broken", 3, 1, {999}); // references an element that does not exist
    assert(!invalid.validate().empty());
    const auto refused = root / "openmodel_model_io_test_refused.omb";
    assert(throwsModelError([&] { ModelBinaryIO::write(refused, invalid); }));
    assert(!std::filesystem::exists(refused));

    for (const auto& p : {path, corrupt, truncated, badMagic})
        std::filesystem::remove(p);
    return 0;
}
