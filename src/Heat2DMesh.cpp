#include <OpenModel/Heat2DMesh.hpp>

#include "Heat2DLinear.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

/**
 * @file Heat2DMesh.cpp
 * @brief Thin-plate conduction on linear triangles, steady and transient (see Heat2DMesh.hpp).
 *
 * Triangle terms use the constant gradients of the linear shape functions. Conduction, source and capacity are
 * integrated with the 3-point rule at the edge midpoints (exact for quadratic integrands). Boundary terms use the 2-point
 * Gauss rule on each edge, as in the structured solver. The time loop and the Picard iteration follow Heat2D.cpp.
 */

namespace OpenModel::Heat2D {

namespace {

using detail::Sparse;
using detail::combine;
using detail::multiply;
using detail::solveFixed;

/** @brief 1 / sqrt(3): abscissa of the 2-point Gauss rule on [-1, 1]. */
const double kGaussPoint = 1.0 / std::sqrt(3.0);

/** @brief Barycentric shape values at the three edge midpoints: the 3-point rule. */
const double kMidpointN[3][3] = {{0.5, 0.5, 0.0}, {0.5, 0.0, 0.5}, {0.0, 0.5, 0.5}};

/** @brief Twice the signed area of a triangle. */
double twiceArea(const TriangleMesh& m, const std::array<int, 3>& t) {
    const auto& p0 = m.nodes[static_cast<std::size_t>(t[0])];
    const auto& p1 = m.nodes[static_cast<std::size_t>(t[1])];
    const auto& p2 = m.nodes[static_cast<std::size_t>(t[2])];
    return (p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1]);
}

/**
 * @brief Conduction matrix K(T) and load vector g(T, t) of the solid, with the boundary terms at time t.
 * @return false and sets err when a material value is not positive.
 */
bool assembleMesh(const TriangleMesh& m, const MeshModel& model, const std::vector<double>& T, double t, Sparse& K,
                  std::vector<double>& g, std::string& err) {
    const std::size_t n = m.nodes.size();
    K = Sparse(n);
    g.assign(n, 0.0);

    for (const auto& tri : m.triangles) {
        const std::size_t i0 = static_cast<std::size_t>(tri[0]);
        const std::size_t i1 = static_cast<std::size_t>(tri[1]);
        const std::size_t i2 = static_cast<std::size_t>(tri[2]);
        const auto& p0 = m.nodes[i0];
        const auto& p1 = m.nodes[i1];
        const auto& p2 = m.nodes[i2];
        const double J2 = twiceArea(m, tri);
        const double area = 0.5 * std::abs(J2);
        const double gx[3] = {(p1[1] - p2[1]) / J2, (p2[1] - p0[1]) / J2, (p0[1] - p1[1]) / J2};
        const double gy[3] = {(p2[0] - p1[0]) / J2, (p0[0] - p2[0]) / J2, (p1[0] - p0[0]) / J2};
        const int nodes[3] = {tri[0], tri[1], tri[2]};

        for (int q = 0; q < 3; ++q) {
            const double x = kMidpointN[q][0] * p0[0] + kMidpointN[q][1] * p1[0] + kMidpointN[q][2] * p2[0];
            const double y = kMidpointN[q][0] * p0[1] + kMidpointN[q][1] * p1[1] + kMidpointN[q][2] * p2[1];
            const double Tgp = kMidpointN[q][0] * T[i0] + kMidpointN[q][1] * T[i1] + kMidpointN[q][2] * T[i2];
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
                g[static_cast<std::size_t>(nodes[a])] += w * qs * kMidpointN[q][a];
            }
        }
    }

    // Boundary terms at time t, 2-point Gauss on each edge.
    const double s = kGaussPoint;
    for (const auto& e : m.edges) {
        const auto it = model.boundaries.find(e.tag);
        if (it == model.boundaries.end()) continue;  // insulated
        const OpenModel::Heat1D::Boundary& bc = it->second;
        if (bc.kind == BoundaryKind::Temperature) continue;
        const auto& pa = m.nodes[static_cast<std::size_t>(e.a)];
        const auto& pb = m.nodes[static_cast<std::size_t>(e.b)];
        const double J = std::hypot(pb[0] - pa[0], pb[1] - pa[1]) / 2.0;
        const std::size_t ia = static_cast<std::size_t>(e.a);
        const std::size_t ib = static_cast<std::size_t>(e.b);
        const double Ta = T[ia];
        const double Tb = T[ib];

        if (bc.kind == BoundaryKind::HeatFlux) {
            if (!bc.value) continue;
            const double qv = bc.value(t);
            for (double sg : {-s, s}) {
                const double Na = 0.5 * (1.0 - sg);
                const double Nb = 0.5 * (1.0 + sg);
                g[ia] += model.thickness * qv * Na * J;
                g[ib] += model.thickness * qv * Nb * J;
            }
            continue;
        }

        const double Tinf = bc.ambient ? bc.ambient(t) : 0.0;
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

/** @brief Capacity matrix: consistent (3-point rule, exact for quadratics) or row-sum lumped. */
bool assembleCapacity(const TriangleMesh& m, const MeshModel& model, Capacity kind, Sparse& C, std::string& err) {
    const std::size_t n = m.nodes.size();
    Sparse consistent(n);
    for (const auto& tri : m.triangles) {
        const auto& p0 = m.nodes[static_cast<std::size_t>(tri[0])];
        const auto& p1 = m.nodes[static_cast<std::size_t>(tri[1])];
        const auto& p2 = m.nodes[static_cast<std::size_t>(tri[2])];
        const double area = 0.5 * std::abs(twiceArea(m, tri));
        const int nodes[3] = {tri[0], tri[1], tri[2]};
        for (int q = 0; q < 3; ++q) {
            const double x = kMidpointN[q][0] * p0[0] + kMidpointN[q][1] * p1[0] + kMidpointN[q][2] * p2[0];
            const double y = kMidpointN[q][0] * p0[1] + kMidpointN[q][1] * p1[1] + kMidpointN[q][2] * p2[1];
            const double rho = model.volumetricHeatCapacity(x, y);
            if (!(rho > 0.0)) {
                err = "volumetric heat capacity must be positive";
                return false;
            }
            const double w = model.thickness * rho * area / 3.0;
            for (int a = 0; a < 3; ++a)
                for (int c = 0; c < 3; ++c) consistent.add(nodes[a], nodes[c], w * kMidpointN[q][a] * kMidpointN[q][c]);
        }
    }
    if (kind == Capacity::Consistent) {
        C = consistent;
        return true;
    }
    C = Sparse(n);
    for (std::size_t i = 0; i < n; ++i) {
        double rowSum = 0.0;
        for (const auto& [j, v] : consistent.rows[i]) rowSum += v;
        if (rowSum != 0.0) C.rows[i][static_cast<int>(i)] = rowSum;
    }
    return true;
}

/** @brief Fixed nodes at time t: every node of a temperature boundary, and every node outside the triangles. */
bool fixedNodes(const TriangleMesh& m, const MeshModel& model, double t, std::vector<char>& fixed,
                std::vector<double>& value, std::string& error) {
    const std::size_t n = m.nodes.size();
    fixed.assign(n, 0);
    value.assign(n, 0.0);
    std::vector<char> touched(n, 0);
    for (const auto& tri : m.triangles)
        for (int v : tri) touched[static_cast<std::size_t>(v)] = 1;
    for (const auto& e : m.edges) {
        const auto it = model.boundaries.find(e.tag);
        if (it == model.boundaries.end() || it->second.kind != BoundaryKind::Temperature) continue;
        const double v = it->second.value(t);
        for (int node : {e.a, e.b}) {
            const std::size_t i = static_cast<std::size_t>(node);
            if (fixed[i] && std::abs(value[i] - v) > 1e-12) {
                error = "two temperature boundaries meet at a node with different temperatures";
                return false;
            }
            fixed[i] = 1;
            value[i] = v;
        }
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (!touched[i]) fixed[i] = 1;
    }
    return true;
}

/** @brief Picard iteration of one (theta) step: coefficients frozen at the iterate, until the change is small. */
bool picardStep(const TriangleMesh& m, const MeshModel& model, const Sparse& C, const std::vector<double>& CT,
                const std::vector<double>& KTn, const std::vector<double>& gn, double tn, double dt, double theta,
                const std::vector<double>& Tstart, const std::vector<char>& fixed, const std::vector<double>& fixedValue,
                const Options& o, std::vector<double>& Tout, int& iterations, std::string& err) {
    const std::size_t n = m.nodes.size();
    std::vector<double> Tk = Tstart;
    const double tnp1 = tn + dt;
    for (int it = 1; it <= o.maxIterations; ++it) {
        Sparse Kk;
        std::vector<double> gk;
        if (!assembleMesh(m, model, Tk, tnp1, Kk, gk, err)) return false;
        Sparse A = (dt > 0.0) ? combine(C, 1.0 / dt, Kk, theta) : Kk;
        std::vector<double> b(n);
        for (std::size_t i = 0; i < n; ++i) {
            // (C / dt + theta K^k) T^{k+1} = (C / dt) T^n - (1 - theta) K^n T^n + (1 - theta) g^n + theta g^k
            // Steady (dt = 0): no capacity term, and theta = 1 leaves b = g^k.
            const double capacity = (dt > 0.0) ? CT[i] / dt : 0.0;
            b[i] = capacity - (1.0 - theta) * KTn[i] + (1.0 - theta) * gn[i] + theta * gk[i];
        }
        std::vector<double> Tnew = Tk;
        if (!solveFixed(A, b, fixed, fixedValue, Tnew, err)) return false;
        double change = 0.0;
        double scale = 1.0;
        for (std::size_t i = 0; i < n; ++i) {
            change = std::max(change, std::abs(Tnew[i] - Tk[i]));
            scale = std::max(scale, std::abs(Tnew[i]));
        }
        Tk = Tnew;
        iterations = it;
        if (change <= o.tolerance * scale) {
            Tout = Tk;
            return true;
        }
    }
    err = "Picard iteration did not converge";
    return false;
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
        const OpenModel::Heat1D::Boundary& b = kv.second;
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
    if (!res.error.empty()) return res;
    if (!o.steady) {
        if (!model.volumetricHeatCapacity) {
            res.error = "volumetric heat capacity is required for a transient run";
            return res;
        }
        if (!(o.timeStep > 0.0)) {
            res.error = "time step must be positive";
            return res;
        }
        if (!(o.endTime > 0.0)) {
            res.error = "end time must be positive";
            return res;
        }
        if (!(o.theta >= 0.0 && o.theta <= 1.0)) {
            res.error = "theta must be in [0, 1]";
            return res;
        }
        if (o.recordEvery < 1) {
            res.error = "recordEvery must be at least 1";
            return res;
        }
    }
    if (!(o.tolerance > 0.0) || o.maxIterations < 1) {
        res.error = "tolerance must be positive and maxIterations at least 1";
        return res;
    }

    const std::size_t n = m.nodes.size();
    std::string err;

    // A steady problem needs a temperature or a convection boundary; the temperature level is otherwise undetermined.
    if (o.steady) {
        bool anchored = false;
        for (const auto& e : m.edges) {
            const auto it = model.boundaries.find(e.tag);
            if (it != model.boundaries.end() && (it->second.kind == BoundaryKind::Temperature ||
                                                  it->second.kind == BoundaryKind::Convection))
                anchored = true;
        }
        if (!anchored) {
            res.error = "a steady problem needs a temperature boundary or a convection boundary: the temperature level is undetermined";
            return res;
        }
        std::vector<char> fixed;
        std::vector<double> fixedValue;
        if (!fixedNodes(m, model, 0.0, fixed, fixedValue, err)) {
            res.error = err;
            return res;
        }
        std::vector<double> T(n, 0.0);
        int its = 0;
        std::vector<double> Tout;
        // Steady: the same Picard loop with no capacity term (dt = 0 means no C/dt).
        Sparse C(n);
        const std::vector<double> zero(n, 0.0);
        if (!picardStep(m, model, C, zero, zero, zero, 0.0, 0.0, 1.0, T, fixed, fixedValue, o, Tout, its, err)) {
            res.error = err;
            res.maxPicardIterations = its;
            return res;
        }
        res.maxPicardIterations = its;
        res.converged = true;
        res.times = {0.0};
        res.temperatures = {Tout};
        return res;
    }

    // Transient.
    std::vector<double> T(n, 0.0);
    if (model.initialTemperature) {
        for (std::size_t i = 0; i < n; ++i) T[i] = model.initialTemperature(m.nodes[i][0], m.nodes[i][1]);
    }
    Sparse C;
    if (!assembleCapacity(m, model, o.capacity, C, err)) {
        res.error = err;
        return res;
    }
    const int steps = std::max(1, static_cast<int>(std::ceil(o.endTime / o.timeStep - 1e-9)));
    const double dt = o.endTime / steps;
    res.times = {0.0};
    res.temperatures = {T};
    res.converged = true;

    for (int step = 0; step < steps; ++step) {
        const double tn = step * dt;
        const double tnp1 = (step + 1) * dt;

        Sparse Kn;
        std::vector<double> gn;
        if (!assembleMesh(m, model, T, tn, Kn, gn, err)) {
            res.error = err;
            return res;
        }
        const std::vector<double> CT = multiply(C, T);
        const std::vector<double> KTn = multiply(Kn, T);

        std::vector<char> fixed;
        std::vector<double> fixedValue;
        if (!fixedNodes(m, model, tnp1, fixed, fixedValue, err)) {
            res.error = err;
            return res;
        }

        std::vector<double> Tnew;
        int its = 0;
        if (!picardStep(m, model, C, CT, KTn, gn, tn, dt, o.theta, T, fixed, fixedValue, o, Tnew, its, err)) {
            res.error = err;
            res.converged = false;
            res.maxPicardIterations = std::max(res.maxPicardIterations, its);
            return res;
        }
        res.maxPicardIterations = std::max(res.maxPicardIterations, its);
        T = Tnew;
        const int done = step + 1;
        if (done % o.recordEvery == 0 || done == steps) {
            res.times.push_back(tnp1);
            res.temperatures.push_back(T);
        }
    }
    return res;
}

} // namespace OpenModel::Heat2D
