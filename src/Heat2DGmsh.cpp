#include <OpenModel/Heat2DGmsh.hpp>

#include <gmsh.h>

#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>

/**
 * @file Heat2DGmsh.cpp
 * @brief Gmsh adapter for the triangle mesh of an annulus (see Heat2DGmsh.hpp).
 */

namespace OpenModel::Heat2D {

TriangleMesh meshAnnulusGmsh(double R, double a, double size, std::string& error) {
    TriangleMesh mesh;
    error.clear();
    if (!(R > a && a > 0.0 && size > 0.0)) {
        error = "annulus needs outerRadius > innerRadius > 0 and a positive size";
        return mesh;
    }
    try {
        gmsh::initialize();
        gmsh::option::setNumber("General.Terminal", 0);
        gmsh::model::add("annulus");

        const int outer = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, R, R);
        const int inner = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, a, a);
        std::vector<std::pair<int, int>> cut;
        std::vector<std::vector<std::pair<int, int>>> cutMap;
        gmsh::model::occ::cut({{2, outer}}, {{2, inner}}, cut, cutMap);
        gmsh::model::occ::synchronize();

        gmsh::option::setNumber("Mesh.CharacteristicLengthMax", size);
        gmsh::option::setNumber("Mesh.CharacteristicLengthMin", size);
        gmsh::model::mesh::generate(2);

        // Nodes: all entities of the model; index by tag.
        std::vector<std::size_t> nodeTags;
        std::vector<double> coords;
        std::vector<double> parametric;
        gmsh::model::mesh::getNodes(nodeTags, coords, parametric);
        std::map<std::size_t, int> index;
        mesh.nodes.reserve(nodeTags.size());
        for (std::size_t k = 0; k < nodeTags.size(); ++k) {
            index[nodeTags[k]] = static_cast<int>(mesh.nodes.size());
            mesh.nodes.push_back({coords[3 * k], coords[3 * k + 1]});
        }

        // Triangles of the surface (element type 2 = 3-node triangle).
        std::vector<int> types;
        std::vector<std::vector<std::size_t>> elementTags;
        std::vector<std::vector<std::size_t>> elementNodes;
        gmsh::model::mesh::getElements(types, elementTags, elementNodes, 2);
        for (std::size_t t = 0; t < types.size(); ++t) {
            if (types[t] != 2) continue;
            const auto& conn = elementNodes[t];
            for (std::size_t e = 0; e + 2 < conn.size(); e += 3) {
                mesh.triangles.push_back({index.at(conn[e]), index.at(conn[e + 1]), index.at(conn[e + 2])});
            }
        }

        // Boundary edges: the curves, classified by their distance from the centre.
        std::vector<std::pair<int, int>> curves;
        gmsh::model::getEntities(curves, 1);
        for (const auto& c : curves) {
            // A node of the curve gives its distance from the centre (the circles are centred at the origin).
            std::vector<double> nc, np;
            std::vector<std::size_t> ntags;
            gmsh::model::mesh::getNodes(ntags, nc, np, 1, c.second, true, false);
            if (ntags.empty()) continue;
            const double r = std::hypot(nc[0], nc[1]);
            const int tag = std::abs(r - R) < std::abs(r - a) ? 1 : 2;

            std::vector<int> ltypes;
            std::vector<std::vector<std::size_t>> ltags;
            std::vector<std::vector<std::size_t>> lnodes;
            gmsh::model::mesh::getElements(ltypes, ltags, lnodes, 1, c.second);
            for (std::size_t t = 0; t < ltypes.size(); ++t) {
                if (ltypes[t] != 1) continue;  // 2-node line
                const auto& conn = lnodes[t];
                for (std::size_t e = 0; e + 1 < conn.size(); e += 2) {
                    mesh.edges.push_back({index.at(conn[e]), index.at(conn[e + 1]), tag});
                }
            }
        }
        gmsh::finalize();
    } catch (const std::exception& ex) {
        error = std::string("Gmsh: ") + ex.what();
        try {
            gmsh::finalize();
        } catch (...) {
        }
        mesh = TriangleMesh{};
    }
    return mesh;
}

} // namespace OpenModel::Heat2D
