#include <OpenModel/MeshQuality.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }

NodeIndex add(MeshData& mesh, double x, double y, double z) { return mesh.addNode(x, y, z); }

const ElementTypeQuality& find(const MeshQualityReport& report, ElementType type) {
    for (const auto& entry : report.types) if (entry.type == type) return entry;
    throw std::runtime_error("The report has no entry for the requested element type");
}
}

int main() {
    try {
        // Ideal elements score 1.
        {
            MeshData mesh;
            std::vector<NodeIndex> cube;
            for (int i = 0; i < 8; ++i) {
                const double x = (i == 1 || i == 2 || i == 5 || i == 6) ? 1.0 : 0.0;
                const double y = (i == 2 || i == 3 || i == 6 || i == 7) ? 1.0 : 0.0;
                const double z = i >= 4 ? 1.0 : 0.0;
                cube.push_back(add(mesh, x, y, z));
            }
            mesh.addElement("hex", ElementType::Hexahedron8, 1, cube);
            const auto report = computeMeshQuality(mesh);
            require(near(find(report, ElementType::Hexahedron8).minimum, 1.0), "unit cube must score 1");
            require(report.acceptable() && report.hasVolumeElements, "the unit cube is acceptable");

            // The same cube with the top face listed in the opposite order is inverted.
            MeshData flipped;
            std::vector<NodeIndex> nodes;
            for (int i = 0; i < 8; ++i) nodes.push_back(add(flipped, mesh.nodeCoordinates(cube[static_cast<std::size_t>(i)])[0],
                mesh.nodeCoordinates(cube[static_cast<std::size_t>(i)])[1], mesh.nodeCoordinates(cube[static_cast<std::size_t>(i)])[2]));
            flipped.addElement("hex", ElementType::Hexahedron8, 1, {nodes[0], nodes[3], nodes[2], nodes[1], nodes[4], nodes[7], nodes[6], nodes[5]});
            const auto bad = computeMeshQuality(flipped);
            require(bad.invertedElements == 1 && !bad.acceptable(), "a mirrored hexahedron must be reported as inverted");
            require(bad.summary().find("inverted") != std::string::npos, "the summary must warn about inverted elements");
        }
        {
            MeshData mesh;
            const auto a = add(mesh, 0, 0, 0), b = add(mesh, 1, 0, 0), c = add(mesh, 0.5, std::sqrt(3.0) / 2.0, 0);
            const auto d = add(mesh, 0.5, std::sqrt(3.0) / 6.0, std::sqrt(2.0 / 3.0));
            mesh.addElement("tri", ElementType::Triangle3, 1, {a, b, c});
            mesh.addElement("tet", ElementType::Tetrahedron4, 2, {a, b, c, d});
            const auto report = computeMeshQuality(mesh);
            require(near(find(report, ElementType::Triangle3).minimum, 1.0), "equilateral triangle must score 1");
            require(near(find(report, ElementType::Tetrahedron4).minimum, 1.0), "regular tetrahedron must score 1");
            require(report.types.front().dimension == 3, "volume types are listed first");
            // Swapping two nodes inverts the tetrahedron.
            MeshData inverted;
            const auto a2 = add(inverted, 0, 0, 0), b2 = add(inverted, 1, 0, 0), c2 = add(inverted, 0.5, std::sqrt(3.0) / 2.0, 0);
            const auto d2 = add(inverted, 0.5, std::sqrt(3.0) / 6.0, std::sqrt(2.0 / 3.0));
            inverted.addElement("tet", ElementType::Tetrahedron4, 1, {a2, c2, b2, d2});
            require(computeMeshQuality(inverted).invertedElements == 1, "a negative-volume tetrahedron is inverted");
        }
        {
            MeshData mesh;
            const auto a = add(mesh, 0, 0, 0), b = add(mesh, 1, 0, 0), c = add(mesh, 1, 1, 0), d = add(mesh, 0, 1, 0);
            mesh.addElement("quad", ElementType::Quadrilateral4, 1, {a, b, c, d});
            require(near(computeMeshQuality(mesh).minimumSurfaceQuality, 1.0), "unit square must score 1");
            // A reflex (arrow-head) quadrilateral is reported as inverted.
            MeshData arrow;
            const auto p0 = add(arrow, 0, 0, 0), p1 = add(arrow, 2, 0, 0), p2 = add(arrow, 0.2, 0.2, 0), p3 = add(arrow, 0, 2, 0);
            arrow.addElement("quad", ElementType::Quadrilateral4, 1, {p0, p1, p2, p3});
            require(computeMeshQuality(arrow).invertedElements == 1, "a reflex quadrilateral must be inverted");
        }
        {
            // Right prism on a right-angled triangle: the 45 degree corners give 1/sqrt(2).
            MeshData mesh;
            std::vector<NodeIndex> n = {add(mesh, 0, 0, 0), add(mesh, 1, 0, 0), add(mesh, 0, 1, 0),
                                        add(mesh, 0, 0, 1), add(mesh, 1, 0, 1), add(mesh, 0, 1, 1)};
            mesh.addElement("prism", ElementType::Prism6, 1, n);
            require(near(computeMeshQuality(mesh).minimumVolumeQuality, std::sqrt(0.5)), "right prism minimum must be 1/sqrt(2)");
            // A pyramid of height 1 over a unit square: base corners give 1/sqrt(1.5).
            MeshData pyramid;
            std::vector<NodeIndex> q = {add(pyramid, 0, 0, 0), add(pyramid, 1, 0, 0), add(pyramid, 1, 1, 0),
                                        add(pyramid, 0, 1, 0), add(pyramid, 0.5, 0.5, 1)};
            pyramid.addElement("pyr", ElementType::Pyramid5, 1, q);
            require(near(computeMeshQuality(pyramid).minimumVolumeQuality, 1.0 / std::sqrt(1.5)), "pyramid base corner value");
        }
        {
            // A needle triangle is counted as poor, not as inverted; the threshold is validated.
            MeshData mesh;
            const auto a = add(mesh, 0, 0, 0), b = add(mesh, 100, 0, 0), c = add(mesh, 50, 1, 0);
            mesh.addElement("tri", ElementType::Triangle3, 1, {a, b, c});
            const auto report = computeMeshQuality(mesh);
            require(report.poorElements == 1 && report.invertedElements == 0, "needle triangle is poor");
            require(report.summary().find("poor") != std::string::npos, "the summary must warn about poor elements");
            bool threw = false;
            try { computeMeshQuality(mesh, 1.5); } catch (const std::invalid_argument&) { threw = true; }
            require(threw, "an invalid threshold must be rejected");
            require(computeMeshQuality(MeshData{}).types.empty(), "an empty mesh has nothing to evaluate");
        }
        std::cout << "mesh_quality_test passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "mesh_quality_test failed: " << e.what() << "\n";
        return 1;
    }
}
