#include <OpenModel/CADFaceMapper.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

/** CAD stand-in with three planar faces: 0 is z = 0, 1 is x = 0 (both within the unit square), 2 is y = 5. */
class PlanarFaces final : public CADModel {
public:
    bool loadFromFile(const std::string&) override { return true; }
    void tessellate(double) override {}
    const std::vector<RenderSubMesh>& getRenderMeshes() const override { return m_none; }
    std::string getFilePath() const override { return {}; }

    bool faceContainsPoint(int faceId, const std::array<double, 3>& p, double tol,
                           const std::array<double, 2>*) const override {
        const auto in = [&](double v) { return v >= -tol && v <= 1.0 + tol; };
        switch (faceId) {
        case 0: return std::abs(p[2]) <= tol && in(p[0]) && in(p[1]);
        case 1: return std::abs(p[0]) <= tol && in(p[1]) && in(p[2]);
        case 2: return std::abs(p[1] - 5.0) <= tol;
        default: return false;
        }
    }

private:
    std::vector<RenderSubMesh> m_none;
};
}

int main() {
    try {
        // Two tetrahedra sharing the face B-C-D. Boundary faces on z = 0: ABC and BEC. On x = 0: ACD.
        MeshData mesh;
        const NodeIndex a = mesh.addNode(0, 0, 0), b = mesh.addNode(1, 0, 0), c = mesh.addNode(0, 1, 0),
                        d = mesh.addNode(0, 0, 1), e = mesh.addNode(1, 1, 0);
        mesh.addElement("tet", ElementType::Tetrahedron4, 1, {a, b, c, d});
        mesh.addElement("tet", ElementType::Tetrahedron4, 2, {b, e, c, d});
        const PlanarFaces cad;

        {
            const auto result = mapCADFacesToMesh(cad, mesh, {0}, 1e-9);
            require(result.boundaryFaceCount == 2, "z = 0 must hold two boundary faces, got " + std::to_string(result.boundaryFaceCount));
            require(result.nodes == std::vector<NodeIndex>({a, b, c, e}), "z = 0 nodes must be A, B, C, E");
            require(result.faceIdsWithoutMatch.empty(), "face 0 matches");
        }
        {
            const auto result = mapCADFacesToMesh(cad, mesh, {1}, 1e-9);
            require(result.boundaryFaceCount == 1 && result.nodes == std::vector<NodeIndex>({a, c, d}),
                    "x = 0 must hold the single face A, C, D");
        }
        {   // Union of two faces, a duplicated id, and a face with no boundary face on it.
            const auto result = mapCADFacesToMesh(cad, mesh, {0, 1, 1, 2}, 1e-9);
            require(result.boundaryFaceCount == 3, "union must hold three boundary faces");
            require(result.nodes.size() == 5, "union must touch all five nodes");
            require(result.faceIdsWithoutMatch == std::vector<int>({2}), "face 2 must be reported without a match");
        }
        {   // The internal face B-C-D lies on neither plane, and more than 64 requested faces are handled.
            std::vector<int> many(100, 7);
            many.push_back(0);
            const auto result = mapCADFacesToMesh(cad, mesh, many, 1e-9);
            require(result.boundaryFaceCount == 2, "chunked search must still find face 0");
            require(result.faceIdsWithoutMatch == std::vector<int>({7}), "unknown id 7 is reported once");
        }
        {   // Tolerance: a node 1e-3 above the plane is outside 1e-9 and inside 1e-2.
            MeshData lifted;
            const NodeIndex a2 = lifted.addNode(0, 0, 0), b2 = lifted.addNode(1, 0, 0), c2 = lifted.addNode(0, 1, 1e-3),
                            d2 = lifted.addNode(0, 0, 1);
            lifted.addElement("tet", ElementType::Tetrahedron4, 1, {a2, b2, c2, d2});
            require(mapCADFacesToMesh(cad, lifted, {0}, 1e-9).nodes.empty(), "a lifted node must not match a tight tolerance");
            require(mapCADFacesToMesh(cad, lifted, {0}, 1e-2).nodes.size() == 3, "it must match a loose tolerance");
        }
        {
            bool thrown = false;
            try { mapCADFacesToMesh(cad, mesh, {0}, 0.0); } catch (const std::invalid_argument&) { thrown = true; }
            require(thrown, "a zero tolerance must be rejected");
            require(mapCADFacesToMesh(cad, MeshData{}, {0}, 1e-9).faceIdsWithoutMatch == std::vector<int>({0}),
                    "an empty mesh matches nothing");
        }
        std::cout << "cad_face_mapper_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "cad_face_mapper_test failed: " << error.what() << "\n";
        return 1;
    }
}
