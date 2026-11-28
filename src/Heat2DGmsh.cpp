#include <OpenModel/Heat2DGmsh.hpp>

#include <gmsh.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

/**
 * @file Heat2DGmsh.cpp
 * @brief Gmsh adapter: builds or imports a planar model, meshes its single surface with linear triangles, and tags the
 *        boundary edges as outer contour (1) or holes (2). See Heat2DGmsh.hpp.
 */

namespace OpenModel::Heat2D {
namespace {

/** @brief Bounding box of an entity: xmin, ymin, zmin, xmax, ymax, zmax. */
struct Box {
    double v[6];
};

Box boxOf(int dim, int tag) {
    Box b{};
    gmsh::model::getBoundingBox(dim, tag, b.v[0], b.v[1], b.v[2], b.v[3], b.v[4], b.v[5]);
    return b;
}

/**
 * @brief Meshes the model currently in Gmsh, which must contain exactly one surface, and returns the triangles and the
 *        tagged boundary edges. The model is not finalised here.
 */
TriangleMesh meshSingleSurface(double size, std::string& error) {
    TriangleMesh mesh;
    std::vector<std::pair<int, int>> surfaces;
    gmsh::model::getEntities(surfaces, 2);
    if (surfaces.size() != 1) {
        error = "the model must contain exactly one planar face, found " + std::to_string(surfaces.size());
        return mesh;
    }
    const int surface = surfaces[0].second;

    // Curves of the surface boundary, and the bounding box of the whole surface.
    std::vector<std::pair<int, int>> boundary;
    gmsh::model::getBoundary({{2, surface}}, boundary, false, false, false);
    const Box whole = boxOf(2, surface);
    const double tol = 1e-9 * std::max({std::abs(whole.v[3] - whole.v[0]), std::abs(whole.v[4] - whole.v[1]), 1.0});

    gmsh::option::setNumber("Mesh.CharacteristicLengthMax", size);
    gmsh::option::setNumber("Mesh.CharacteristicLengthMin", size);
    gmsh::model::mesh::generate(2);

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

    std::vector<int> types;
    std::vector<std::vector<std::size_t>> elementTags;
    std::vector<std::vector<std::size_t>> elementNodes;
    gmsh::model::mesh::getElements(types, elementTags, elementNodes, 2, surface);
    for (std::size_t t = 0; t < types.size(); ++t) {
        if (types[t] != 2) {  // 3-node triangle
            error = "the surface mesh is not made of triangles";
            return TriangleMesh{};
        }
        const auto& conn = elementNodes[t];
        for (std::size_t e = 0; e + 2 < conn.size(); e += 3) {
            mesh.triangles.push_back({index.at(conn[e]), index.at(conn[e + 1]), index.at(conn[e + 2])});
        }
    }

    for (const auto& c : boundary) {
        if (c.first != 1) continue;
        const Box cb = boxOf(1, c.second);
        // Outer contour: the curve reaches a side of the surface's bounding box.
        const bool outer = std::abs(cb.v[0] - whole.v[0]) < tol || std::abs(cb.v[3] - whole.v[3]) < tol ||
                           std::abs(cb.v[1] - whole.v[1]) < tol || std::abs(cb.v[4] - whole.v[4]) < tol;
        const int tag = outer ? 1 : 2;

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
    return mesh;
}

/** @brief Runs a function that uses Gmsh and always finalises Gmsh; failures are returned as text. */
template <typename Fn>
TriangleMesh withGmsh(Fn&& body, std::string& error) {
    error.clear();
    TriangleMesh mesh;
    try {
        gmsh::initialize();
        gmsh::option::setNumber("General.Terminal", 0);
        mesh = body(error);
        gmsh::finalize();
    } catch (const std::exception& ex) {
        error = std::string("Gmsh: ") + ex.what();
        try {
            gmsh::finalize();
        } catch (...) {
        }
        mesh = TriangleMesh{};
    }
    if (!error.empty()) return TriangleMesh{};
    return mesh;
}

} // namespace

TriangleMesh meshAnnulusGmsh(double R, double a, double size, std::string& error) {
    error.clear();
    if (!(R > a && a > 0.0 && size > 0.0)) {
        error = "annulus needs outerRadius > innerRadius > 0 and a positive size";
        return TriangleMesh{};
    }
    return withGmsh(
        [&](std::string& err) {
            gmsh::model::add("annulus");
            const int outer = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, R, R);
            const int inner = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, a, a);
            std::vector<std::pair<int, int>> cut;
            std::vector<std::vector<std::pair<int, int>>> cutMap;
            gmsh::model::occ::cut({{2, outer}}, {{2, inner}}, cut, cutMap);
            gmsh::model::occ::synchronize();
            return meshSingleSurface(size, err);
        },
        error);
}

TriangleMesh meshPlanarStepGmsh(const std::string& path, double size, std::string& error) {
    error.clear();
    if (!(size > 0.0)) {
        error = "the element size must be positive";
        return TriangleMesh{};
    }
    return withGmsh(
        [&](std::string& err) {
            gmsh::model::add("imported");
            std::vector<std::pair<int, int>> imported;
            gmsh::model::occ::importShapes(path, imported, false);
            gmsh::model::occ::synchronize();
            if (imported.empty()) {
                err = "the file has no shapes or cannot be read: " + path;
                return TriangleMesh{};
            }
            return meshSingleSurface(size, err);
        },
        error);
}

} // namespace OpenModel::Heat2D
