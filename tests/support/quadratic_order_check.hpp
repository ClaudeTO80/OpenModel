#pragma once
/**
 * @file quadratic_order_check.hpp
 * @brief Test helper: checks the mid-edge node order of quadratic simplex elements.
 *
 * OpenModel stores 10-node tetrahedra and 6-node triangles in the Abaqus/code_aster order. Mid-edge nodes
 * 4..9 of a Tetrahedron10 lie on the edges 0-1, 1-2, 2-0, 0-3, 1-3, 2-3; mid-edge nodes 3..5 of a Triangle6 on
 * 0-1, 1-2, 2-0. For planar faces and straight edges each mid node is the exact edge midpoint, which makes a
 * wrong order observable as a distance to the expected midpoint.
 */
#include <OpenModel/MeshData.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>

namespace quadratic_order {

/** @brief Largest distance of a mid node to the midpoint of the edge the Abaqus order assigns to it. */
inline double maxMidNodeDeviation(const MeshData& mesh, std::size_t& tetrahedra, std::size_t& triangles) {
    static const int tetEdges[6][2] = {{0, 1}, {1, 2}, {2, 0}, {0, 3}, {1, 3}, {2, 3}};
    static const int triEdges[3][2] = {{0, 1}, {1, 2}, {2, 0}};
    double worst = 0.0;
    tetrahedra = triangles = 0;
    for (const auto& block : mesh.getElementBlocks()) {
        const bool tet = block.type == ElementType::Tetrahedron10;
        if (!tet && block.type != ElementType::Triangle6) continue;
        const std::size_t corners = tet ? 4 : 3, mids = tet ? 6 : 3, per = tet ? 10 : 6;
        for (std::size_t e = 0; e < block.size(); ++e) {
            (tet ? tetrahedra : triangles) += 1;
            for (std::size_t m = 0; m < mids; ++m) {
                const int* edge = tet ? tetEdges[m] : triEdges[m];
                const auto a = mesh.nodeCoordinates(block.connectivity[e * per + static_cast<std::size_t>(edge[0])]);
                const auto b = mesh.nodeCoordinates(block.connectivity[e * per + static_cast<std::size_t>(edge[1])]);
                const auto p = mesh.nodeCoordinates(block.connectivity[e * per + corners + m]);
                double d2 = 0.0;
                for (int i = 0; i < 3; ++i) { const double d = p[i] - 0.5 * (a[i] + b[i]); d2 += d * d; }
                worst = std::max(worst, std::sqrt(d2));
            }
        }
    }
    return worst;
}

} // namespace quadratic_order
