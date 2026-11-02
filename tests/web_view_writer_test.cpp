#include <OpenModel/WebViewWriter.hpp>

#include <cassert>
#include <map>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

/** Structured grid with n cells per side, built as hexahedra or as six tetrahedra per cell. */
MeshData makeGrid(std::size_t n, bool hex) {
    MeshData mesh;
    const std::size_t s = n + 1;
    auto idx = [s](std::size_t x, std::size_t y, std::size_t z) { return static_cast<NodeIndex>((z * s + y) * s + x); };
    for (std::size_t z = 0; z <= n; ++z)
        for (std::size_t y = 0; y <= n; ++y)
            for (std::size_t x = 0; x <= n; ++x) mesh.addNode(1000 + idx(x, y, z), double(x), double(y), double(z));
    ElementId id = 1;
    for (std::size_t z = 0; z < n; ++z)
        for (std::size_t y = 0; y < n; ++y)
            for (std::size_t x = 0; x < n; ++x) {
                const NodeIndex a = idx(x, y, z), b = idx(x + 1, y, z), c = idx(x, y + 1, z), d = idx(x + 1, y + 1, z);
                const NodeIndex e = idx(x, y, z + 1), f = idx(x + 1, y, z + 1), g = idx(x, y + 1, z + 1), h = idx(x + 1, y + 1, z + 1);
                if (hex) {
                    mesh.addElement("hex", ElementType::Hexahedron8, id++, {a, b, d, c, e, f, h, g});
                } else {
                    const NodeIndex t[6][4] = {{a, b, d, h}, {a, d, c, h}, {a, c, g, h}, {a, g, e, h}, {a, e, f, h}, {a, f, b, h}};
                    for (const auto& q : t) mesh.addElement("tet", ElementType::Tetrahedron4, id++, {q[0], q[1], q[2], q[3]});
                }
            }
    return mesh;
}

std::uint32_t le32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

/** Minimal view of a parsed image: descriptor text and payload pointers. */
struct Parsed {
    std::string json;
    const unsigned char* bin = nullptr;
    std::size_t binSize = 0;

    long number(const std::string& key, std::size_t from = 0) const {
        const auto k = json.find("\"" + key + "\":", from);
        assert(k != std::string::npos);
        return std::stol(json.substr(k + key.size() + 3));
    }
    std::size_t chunkOffset(const std::string& name) const {
        const auto k = json.find("\"" + name + "\":{");
        assert(k != std::string::npos);
        return static_cast<std::size_t>(number("offset", k));
    }
    std::size_t chunkCount(const std::string& name) const {
        const auto k = json.find("\"" + name + "\":{");
        assert(k != std::string::npos);
        return static_cast<std::size_t>(number("count", k));
    }
};

Parsed parse(const std::vector<unsigned char>& image) {
    assert(image.size() > 24);
    assert(std::memcmp(image.data(), "MGWV\r\n\x1a\n", 8) == 0);
    assert(le32(&image[8]) == 1);
    const std::size_t jsonSize = le32(&image[12]);
    const std::size_t binSize = le32(&image[16]);
    assert(jsonSize % 4 == 0);
    assert(24 + jsonSize + binSize == image.size());
    Parsed p;
    p.json.assign(reinterpret_cast<const char*>(&image[24]), jsonSize);
    p.bin = image.data() + 24 + jsonSize;
    p.binSize = binSize;
    return p;
}

void checkIndexRanges(const Parsed& p, const WebViewResult& r) {
    const auto tri = p.chunkOffset("triIndices");
    assert(p.chunkCount("triIndices") == r.triangles * 3);
    for (std::size_t i = 0; i < r.triangles * 3; ++i) assert(le32(p.bin + tri + i * 4) < r.vertices);
    const auto edge = p.chunkOffset("edgeIndices");
    assert(p.chunkCount("edgeIndices") == r.edges * 2);
    for (std::size_t i = 0; i < r.edges * 2; ++i) assert(le32(p.bin + edge + i * 4) < r.vertices);
    assert(p.chunkCount("edgeBlock") == r.edges);
    assert(p.chunkOffset("positions") % 4 == 0 && p.chunkOffset("triIndices") % 4 == 0);
}

} // namespace

int main() {
    // Tetrahedral 2x2x2 cube: 48 tets, 120 unique faces, 48 on the skin.
    {
        const auto mesh = makeGrid(2, false);
        std::vector<unsigned char> image;
        const auto r = WebViewWriter::build(mesh, {}, image);
        assert(r.success);
        assert(r.boundaryTriangles == 48);
        assert(r.triangles == 120);
        assert(r.vertices == 27);
        const auto p = parse(image);
        assert(p.number("triangles") == 120 && p.number("boundaryTriangles") == 48);
        checkIndexRanges(p, r);

        WebViewOptions skinOnly;
        skinOnly.includeInteriorFaces = false;
        const auto s = WebViewWriter::build(mesh, skinOnly, image);
        assert(s.success && s.triangles == 48 && s.boundaryTriangles == 48);
        assert(s.vertices == 26); // the centre node is not on the skin
        checkIndexRanges(parse(image), s);
    }
    // Hexahedral 2x2x2 cube: 24 skin quads, 12 interior quads, 48 skin edges.
    {
        const auto mesh = makeGrid(2, true);
        std::vector<unsigned char> image;
        const auto r = WebViewWriter::build(mesh, {}, image);
        assert(r.success);
        assert(r.boundaryTriangles == 48);
        assert(r.triangles == 72);
        assert(r.boundaryEdges == 48);
        checkIndexRanges(parse(image), r);

        // Picking chunks: external ids, and a consistent face/element mapping checked against the mesh itself.
        const auto p = parse(image);
        assert(p.chunkCount("vertexNodeId") == r.vertices && p.chunkCount("triElementId") == r.triangles && p.chunkCount("triFace") == r.triangles);
        const auto vn = p.chunkOffset("vertexNodeId"), te = p.chunkOffset("triElementId"), tf = p.chunkOffset("triFace");
        const auto ti = p.chunkOffset("triIndices"), pos = p.chunkOffset("positions");
        std::map<std::uint32_t, int> trianglesPerFace;
        for (std::size_t v = 0; v < r.vertices; ++v) {
            const auto id = le32(p.bin + vn + v * 4);
            assert(id >= 1000 && id < 1027);
            float xyz[3];
            std::memcpy(xyz, p.bin + pos + v * 12, 12);
            const auto c = mesh.nodeCoordinates(mesh.nodeIndex(id));
            for (int k = 0; k < 3; ++k) assert(std::fabs(double(xyz[k]) + 1.0 - c[k]) < 1e-6);   // origin is the centre (1,1,1)
        }
        const auto& hexBlock = mesh.getElementBlocks()[0];
        for (std::size_t t = 0; t < r.triangles; ++t) {
            const auto element = le32(p.bin + te + t * 4);
            assert(element >= 1 && element <= 8);
            const auto* conn = &hexBlock.connectivity[(element - 1) * 8];
            for (int k = 0; k < 3; ++k) {
                const auto nodeId = le32(p.bin + vn + le32(p.bin + ti + (t * 3 + k) * 4) * 4);
                bool found = false;
                for (int c = 0; c < 8; ++c) found = found || mesh.nodeId(conn[c]) == nodeId;
                assert(found);   // every corner of a triangle belongs to the element that owns it
            }
            ++trianglesPerFace[le32(p.bin + tf + t * 4)];
        }
        assert(trianglesPerFace.size() == 36);   // 24 skin + 12 interior quads
        for (const auto& kv : trianglesPerFace) assert(kv.second == 2);
        // Skin faces come first in both arrays: the face index of the first boundary triangles is below 24.
        for (std::size_t t = 0; t < r.boundaryTriangles; ++t) assert(le32(p.bin + tf + t * 4) < 24);
    }
    // 2D elements coincident with the skin carry the surface group; regions come from volume groups.
    {
        auto mesh = makeGrid(1, false);
        // Bottom face of the cube (z = 0): nodes 0,1,2,3 in the grid numbering.
        mesh.addElement("bottom", ElementType::Triangle3, 500, {0, 1, 3});
        mesh.addPhysicalGroup("floor", 2, 1, {500});
        std::vector<ElementId> all;
        for (const auto& b : mesh.getElementBlocks()) if (b.type == ElementType::Tetrahedron4) all = b.ids;
        mesh.addPhysicalGroup("solid", 3, 2, all, "steel");
        std::vector<unsigned char> image;
        const auto r = WebViewWriter::build(mesh, {}, image);
        assert(r.success);
        assert(r.boundaryTriangles == 12); // the shell element is merged, not duplicated
        const auto p = parse(image);
        const auto surf = p.chunkOffset("triSurface");
        const auto region = p.chunkOffset("triRegion");
        std::size_t floors = 0;
        for (std::size_t t = 0; t < r.triangles; ++t) {
            if (le32(p.bin + surf + t * 4) == 1) ++floors;
            assert(le32(p.bin + region + t * 4) == 2);
        }
        assert(floors == 1);
        // The shell element's block ("bottom") owns the face it coincides with; the others stay with the volume block.
        const auto triBlock = p.chunkOffset("triBlock");
        std::size_t shellBlockFaces = 0;
        for (std::size_t t = 0; t < r.triangles; ++t)
            if (le32(p.bin + triBlock + t * 4) == 1) ++shellBlockFaces; // block index 1 = "bottom"
        assert(shellBlockFaces == 1);
        assert(p.json.find("\"material\":\"steel\"") != std::string::npos);
        assert(p.json.find("\"elements\":6,") != std::string::npos); // group "solid": the 6 tetrahedra
        assert(p.chunkCount("edgeRegion") == r.edges);
        const auto edgeRegion = p.chunkOffset("edgeRegion");
        for (std::size_t e = 0; e < r.edges; ++e) assert(le32(p.bin + edgeRegion + e * 4) == 2); // every edge belongs to "solid"
        // Optional group colour from the CAD.
        WebViewOptions colored;
        colored.groupColors["solid"] = {{1.0f, 0.0f, 0.5f}};
        std::vector<unsigned char> coloredImage;
        assert(WebViewWriter::build(mesh, colored, coloredImage).success);
        assert(parse(coloredImage).json.find("\"color\":\"#ff0080\"") != std::string::npos);
        assert(p.json.find("\"color\"") == std::string::npos);
    }
    // Interface between two blocks is exported even without interior faces; lines become edges.
    {
        MeshData mesh;
        const auto a = mesh.addNode(0, 0, 0), b = mesh.addNode(1, 0, 0), c = mesh.addNode(0, 1, 0),
                   d = mesh.addNode(0, 0, 1), e = mesh.addNode(1, 1, 1);
        mesh.addElement("left", ElementType::Tetrahedron4, 1, {a, b, c, d});
        mesh.addElement("right", ElementType::Tetrahedron4, 2, {b, c, d, e});
        mesh.addElement("beam", ElementType::Line2, 3, {a, e});
        WebViewOptions o;
        o.includeInteriorFaces = false;
        std::vector<unsigned char> image;
        const auto r = WebViewWriter::build(mesh, o, image);
        assert(r.success);
        assert(r.boundaryTriangles == 6);
        assert(r.triangles == 7);     // 6 skin + 1 material interface
        assert(r.boundaryEdges == 10); // 9 skin edges + the beam
    }
    // Scalar fields, validation of inputs and file output.
    {
        const auto mesh = makeGrid(1, false);
        WebViewOptions o;
        WebNodalField field;
        field.name = "z \"height\"";
        for (std::size_t n = 0; n < mesh.getNumNodes(); ++n) field.values.push_back(mesh.nodeCoordinates(n)[2] * 10.0);
        o.fields.push_back(field);
        std::vector<unsigned char> image;
        auto r = WebViewWriter::build(mesh, o, image);
        assert(r.success);
        const auto p = parse(image);
        assert(p.json.find("\"name\":\"z \\\"height\\\"\"") != std::string::npos);
        assert(p.json.find("\"min\":0,\"max\":10,") != std::string::npos);

        o.fields[0].values.pop_back();
        r = WebViewWriter::build(mesh, o, image);
        assert(!r.success && !r.error.empty() && image.empty());

        const auto path = std::filesystem::temp_directory_path() / "openmodel_web_view_test.mgwv";
        const auto written = WebViewWriter::writeFile(path, mesh);
        assert(written.success && std::filesystem::file_size(path) == written.bytesWritten);
        std::filesystem::remove(path);

        MeshData empty;
        assert(!WebViewWriter::build(empty, {}, image).success);
    }
    std::cout << "web_view_writer_test passed\n";
    return 0;
}
