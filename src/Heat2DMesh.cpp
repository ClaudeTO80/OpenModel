#include <OpenModel/Heat2DMesh.hpp>

#include "Heat2DLinear.hpp"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

/**
 * @file Heat2DMesh.cpp
 * @brief Steady thin-plate conduction on linear triangles (see Heat2DMesh.hpp).
 *
 * Triangle terms use the constant gradients of the linear shape functions. The conduction, source and capacity-free
 * terms are integrated with the 3-point rule at the edge midpoints (exact for quadratic integrands). Edge terms use the
 * 2-point Gauss rule on each boundary edge, as in the structured solver.
 */

namespace OpenModel::Heat2D {

namespace {

using detail::Sparse;
using detail::multiply;
using detail::solveFixed;

/** @brief 1 / sqrt(3): abscissa of the 2-point Gauss rule on [-1, 1]. */
const double kGaussPoint = 1.0 / std::sqrt(3.0);

/** @brief Twice the signed area of a triangle. */
double twiceArea(const TriangleMesh& m, const std::array<int, 3>& t) {
    const auto& p0 = m.nodes[static_cast<std::size_t>(t[0])];
    const auto& p1 = m.nodes[static_cast<std::size_t>(t[1])];
    const auto& p2 = m.nodes[static_cast<std::size_t>(t[2])];
    return (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1]);
}

/**
 * @brief Conduction matrix K(T) and load vector g(T, t) of the solid, with the boundary terms.
 * @return false and sets err when a material value is not positive.
 */
bool assembleMesh(const TriangleMesh& m, const MeshModel& model, const std::vector<double>& T, double t, Sparse& K,
                  std::vector<double>& g, std::string& err) {
    const std::size_t n = m.nodes.size();
    K = Sparse(n);
    g.assign(n, 0.0);

    // 3-point rule: the edge midpoints, with the barycentric shape values Nq and weight 1/3 of the area each.
    const double Nq[3][3] = {{0.5, 0.5, 0.0}, {0.5, 0.0, 0.5}, {0.0, 0.5, 0.5}};

    for (const auto& tri : m.triangles) {
        const std::size_t i0 = static_cast<std::size_t>(tri[0]);
        const std::size_t i1 = static_cast<std::size_t>(tri[1]);
        const std::size_t i2 = static_cast<std::size_t>(tri[2]);
        const auto& p0 = m.nodes[i0];
        const auto& p1 = m.nodes[i1];
        const auto& p2 = m.nodes[i2];
        const double J2 = twiceArea(m, tri);
        const double area = 0.5 * std::abs(J2);

        // Gradients of the linear shape functions (constant on the triangle).
        const double gx[3] = {(p1[1] - p2[1]) / J2, (p2[1] - p0[1]) / J2, (p0[1] - p1[1]) / J2};
        const double gy[3] = {(p2[0] - p1[0]) / J2, (p0[0] - p2[0]) / J2, (p1[0] - p0[0]) / J2};
        const int nodes[3] = {tri[0], tri[1], tri[2]};

        for (int q = 0; q < 3; ++q) {
            const double x = Nq[q][0] * p0[0] + Nq[q][1] * p1[0] + Nq[q][2] * p2[0];
            const double y = Nq[q][0] * p0[1] + Nq[q][1] * p1[1] + Nq[q][2] * p2[1];
            const double Tgp = Nq[q][0] * T[i0] + Nq[q][1] * T[i1] + Nq[q][2] * T[i2];
            const double k = model.conductivity(x, y, Tgp);
            if (!(k > 0.0)) {
                err = "conductivity must be positive";
                return false;
            }
            const double qs = model.source ? model.source(x, y, t) : 0.0;
            const double w = model.thickness * area / 3.0;
            for (int a = 0; a < 3; ++a) {
                for (int c = 0; c < 3; ++c) {
                    K.add(nodes[a], nodes[c], w * k * (gx[a] * gx[c] + gy[a] * gy[c]));
                }
                g[static_cast<std::size_t>(nodes[a])] += w * qs * Nq[q][a];
            }
        }
    }

    // Boundary terms, 2-point Gauss on each edge.
    const double s = kGaussPoint;
    for (const auto& e : m.edges) {
        const auto it = model.boundaries.find(e.tag);
        if (it == model.boundaries.end()) continue;  // insulated
        const Heat1D::Boundary& bc = it->second;
        if (bc.kind == BoundaryKind::Temperature) continue;
        const auto& pa = m.nodes[static_cast<std::size_t>(e.a)];
        const auto& pb = m.nodes[static_cast<std::size_t>(e.b)];
        const double len = std::hypot(pb[0] - pa[0], pb[1] - pa[1]);
        const double J = len / 2.0;
        const std::size_t ia = static_cast<std::size_t>(e.a);
        const std::size_t ib = static_cast<std::size_t>(e.b);
        const double Ta = T[ia];
        const double Tb = T[ib];

        if (bc.kind == BoundaryKind::HeatFlux) {
            if (!bc.value) continue;
            const double qv = bc.value(0.0);
            for (double sg : {-s, s}) {
                const double Na = 0.5 * (1.0 - sg);
                const double Nb = 0.5 * (1.0 + sg);
                g[ia] += model.thickness * qv * Na * J;
                g[ib] += model.thickness * qv * Nb * J;
            }
            continue;
        }

        const double Tinf = bc.ambient ? bc.ambient(0.0) : 0.0;
        for (double sg : {-s, s}) {
            const double Na = 0.5 * (1.0 - sg);
            const double Nb = 0.5 * (1.0 + sg);
            const double Tgp = Na * Ta + Nb * Tb;
            const double a = bc.h + bc.emissivity * model.stefanBoltzmann * (Tgp * Tgp + Tinf * Tinf) * (Tgp + Tinf);
            const double w = model.thickness * a * J;
            K.add(e.a, e.a, w * Na * Na);
            K.add(e.a, e.b, w * Na * Nb);
            K.add(e.b, e.a, w * Nb * Na);
            K.add(e.b, e.b, w * Nb * Nb);
            g[ia] += model.thickness * a * Tinf * Na * J;
            g[ib] += model.thickness * a * Tinf * Nb * J;
        }
    }
    return true;
}

} // namespace

std::string validateMesh(const TriangleMesh& m, const MeshModel& model) {
    if (m.nodes.empty() || m.triangles.empty()) return "the mesh has no nodes or no triangles";
    if (!(model.thickness > 0.0)) return "thickness must be positive";
    if (!model.conductivity) return "conductivity is required";
    const int n = static_cast<int>(m.nodes.size());
    for (const auto& tri : m.triangles) {
        for (int v : tri) {
            if (v < 0 || v >= n) return "a triangle refers to a node that does not exist";
        }
        if (!(std::abs(twiceArea(m, tri)) > 0.0)) return "a triangle has zero area";
    }
    for (const auto& e : m.edges) {
        if (e.a < 0 || e.a >= n || e.b < 0 || e.b >= n) return "a boundary edge refers to a node that does not exist";
    }
    for (const auto& kv : model.boundaries) {
        const Heat1D::Boundary& b = kv.second;
        if (b.kind == BoundaryKind::Temperature && !b.value) return "a temperature boundary needs a temperature value";
        if (b.kind == BoundaryKind::Convection) {
            if (!b.ambient) return "a convection boundary needs an ambient temperature";
            if (!(b.h >= 0.0)) return "the convection coefficient must not be negative";
            if (!(b.emissivity >= 0.0 && b.emissivity <= 1.0)) return "the emissivity must be in [0, 1]";
        }
    }
    return "";
}

MeshResult solveMesh(const TriangleMesh& m, const MeshModel& model, const Options& o) {
    MeshResult res;
    res.error = validateMesh(m, model);
    if (res.error.empty() && !o.steady) res.error = "only the steady problem is available on a mesh";
    if (!res.error.empty()) return res;

    const std::size_t n = m.nodes.size();

    // Fixed nodes: every node of a temperature edge; a node shared by two temperature edges must have one value.
    std::vector<char> fixed(n, 0);
    std::vector<double> fixedValue(n, 0.0);
    std::vector<char> touched(n, 0);
    for (const auto& tri : m.triangles)
        for (int v : tri) touched[static_cast<std::size_t>(v)] = 1;
    for (const auto& e : m.edges) {
        const auto it = model.boundaries.find(e.tag);
        if (it == model.boundaries.end() || it->second.kind != BoundaryKind::Temperature) continue;
        const double v = it->second.value(0.0);
        for (int node : {e.a, e.b}) {
            const std::size_t i = static_cast<std::size_t>(node);
            if (fixed[i] && std::abs(fixedValue[i] - v) > 1e-12) {
                res.error = "two temperature boundaries meet at a node with different temperatures";
                return res;
            }
            fixed[i] = 1;
            fixedValue[i] = v;
        }
    }
    // Nodes outside every triangle are not part of the solid: fixed to zero, out of the system.
    bool anchored = false;
    for (std::size_t i = 0; i < n; ++i) {
        if (!touched[i]) fixed[i] = 1;
        if (fixed[i]) anchored = true;
    }
    for (const auto& e : model.boundaries) {
        if (e.second.kind == BoundaryKind::Convection) anchored = true;
    }
    if (!anchored) {
        res.error = "a steady problem needs a temperature boundary or a convection boundary: the temperature level is undetermined";
        return res;
    }

    std::string err;
    std::vector<double> Tk(n, 0.0);
    for (int it = 1; it <= o.maxIterations; ++it) {
        Sparse K;
        std::vector<double> g;
        if (!assembleMesh(m, model, Tk, 0.0, K, g, err)) {
            res.error = err;
            return res;
        }
        std::vector<double> Tnew = Tk;
        if (!solveFixed(K, g, fixed, fixedValue, Tnew, err)) {
            res.error = err;
            return res;
        }
        double change = 0.0;
        double scale = 1.0;
        for (std::size_t i = 0; i < n; ++i) {
            change = std::max(change, std::abs(Tnew[i] - Tk[i]));
            scale = std::max(scale, std::abs(Tnew[i]));
        }
        Tk = Tnew;
        res.maxPicardIterations = it;
        if (change <= o.tolerance * scale) {
            res.converged = true;
            break;
        }
    }
    res.temperatures = Tk;
    if (!res.converged) res.error = "Picard iteration did not converge";
    return res;
}

} // namespace OpenModel::Heat2D
