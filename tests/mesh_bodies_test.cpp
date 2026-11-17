/**
 * @file mesh_bodies_test.cpp
 * @brief Export rule of the bodies: a volume body writes its volume elements only, a surface body its surface elements.
 */
#include <OpenModel/AbaqusInpWriter.hpp>
#include <OpenModel/CodeAster.hpp>
#include <OpenModel/MeshBodies.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace OpenModel;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n";        \
        }                                                                                  \
    } while (0)

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

/**
 * Tetrahedron 1 (nodes 1-4, body A, volume) with its faces 2 and 3 (body A, surface elements of a volume body), and a
 * separate triangle 4 (nodes 5-7, body B, a sheet meshed in surface).
 */
MeshData makeBodies(bool withBodies) {
    MeshData m;
    m.addNode(1, 0, 0, 0);
    m.addNode(2, 1, 0, 0);
    m.addNode(3, 0, 1, 0);
    m.addNode(4, 0, 0, 1);
    m.addNode(5, 5, 0, 0);
    m.addNode(6, 6, 0, 0);
    m.addNode(7, 5, 1, 0);
    m.addElement("VOL", ElementType::Tetrahedron4, 1, {0, 1, 2, 3});
    m.addElement("SURF", ElementType::Triangle3, 2, {0, 1, 2});
    m.addElement("SURF", ElementType::Triangle3, 3, {0, 1, 3});
    m.addElement("SHEET", ElementType::Triangle3, 4, {4, 5, 6});
    if (withBodies) {
        MeshBody a;
        a.name = "Solid";
        a.dimension = 3;
        a.elementIds = {1, 2, 3};
        MeshBody b;
        b.name = "Sheet";
        b.dimension = 2;
        b.elementIds = {4};
        m.setBodies({a, b});
    }
    return m;
}

std::size_t countElements(const MeshData& m) { return m.getNumElements(); }

bool hasElement(const MeshData& m, ElementId id) {
    for (const auto& block : m.getElementBlocks())
        for (const ElementId e : block.ids) if (e == id) return true;
    return false;
}

void testRule() {
    const MeshData m = makeBodies(true);
    const auto excluded = nonExportedElements(m);
    CHECK(excluded.size() == 2 && excluded.count(2) && excluded.count(3));   // faces of the volume body
    CHECK(!excluded.count(1) && !excluded.count(4));                         // volume and sheet elements are written

    const MeshData out = exportableMesh(m);
    CHECK(hasElement(out, 1) && hasElement(out, 4));
    CHECK(!hasElement(out, 2) && !hasElement(out, 3));
    CHECK(out.getNumNodes() == m.getNumNodes());                             // nodes are kept
    CHECK(out.getBodies().size() == 2 && out.getBodies()[0].elementIds == std::vector<ElementId>{1});
    CHECK(countElements(out) == 2);

    // Without bodies nothing is excluded: the mesh is exported as before.
    const MeshData plain = makeBodies(false);
    CHECK(nonExportedElements(plain).empty());
    CHECK(countElements(exportableMesh(plain)) == countElements(plain));
}

void testExporters() {
    const fs::path dir = fs::temp_directory_path() / ("mg_bodies_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(dir);
    const MeshData m = makeBodies(true);

    AbaqusWriteOptions options;
    const auto r = AbaqusInpWriter::writeFile((dir / "bodies.inp").string(), m, options);
    if (!r.success) for (const auto& e : r.errors) std::cerr << e << "\n";
    CHECK(r.success);
    const std::string inp = slurp(dir / "bodies.inp");
    CHECK(inp.find("\n1, 1, 2, 3, 4") != std::string::npos);     // the volume element is written
    CHECK(inp.find("\n4, 5, 6, 7") != std::string::npos);        // the sheet element is written
    CHECK(inp.find("\n2, 1, 2, 3") == std::string::npos);        // the face of the volume body is not
    CHECK(inp.find("\n3, 1, 2, 4") == std::string::npos);

    const std::string errorText = writeCodeAsterMesh(dir / "bodies.mail", m);
    CHECK(errorText.empty());
    const std::string mail = slurp(dir / "bodies.mail");
    CHECK(mail.find("M1 ") != std::string::npos || mail.find("M1\t") != std::string::npos);   // volume element
    CHECK(mail.find("M2 ") == std::string::npos && mail.find("M3 ") == std::string::npos);    // faces left out
    fs::remove_all(dir);
}

} // namespace

int main() {
    testRule();
    testExporters();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "mesh_bodies_test: all checks passed\n";
    return 0;
}
