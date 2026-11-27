#include <OpenModel/Heat2D.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

/**
 * @file Heat2D.cpp
 * @brief Implementation of the two-dimensional thin-plate heat solver. See Heat2D.hpp for the model.
 *
 * Element matrices use 2x2 Gauss quadrature on bilinear quadrilaterals. Edge terms use 2-point Gauss quadrature on the
 * edge. The global matrices are stored by rows (std::map) and are symmetric; the Dirichlet rows are eliminated
 * symmetrically so the conjugate-gradient solver with a Jacobi preconditioner applies.
 */

namespace OpenModel::Heat2D {

namespace {

/** @brief 1 / sqrt(3): abscissa of the 2-point Gauss rule on [-1, 1]. */
const double kGaussPoint = 1.0 / std::sqrt(3.0);

/** @brief Sparse symmetric-in-structure matrix stored by rows. */
struct Sparse {
    std::vector<std::map<int, double>> rows;

    Sparse() = default;
    explicit Sparse(std::size_t n) : rows(n) {}

    void add(int i, int j, double v) { rows[static_cast<std::size_t>(i)][j] += v; }
};

/** @brief a * A + b * B. */
Sparse combine(const Sparse& A, double a, const Sparse& B, double b) {
    Sparse C(A.rows.size());
    for (std::size_t i = 0; i < A.rows.size(); ++i) {
        for (const auto& [j, v] : A.rows[i]) C.rows[i][j] += a * v;
        for (const auto& [j, v] : B.rows[i]) C.rows[i][j] += b * v;
    }
    return C;
}

/** @brief y = A x. */
std::vector<double> multiply(const Sparse& A, const std::vector<double>& x) {
    std::vector<double> y(A.rows.size(), 0.0);
    for (std::size_t i = 0; i < A.rows.size(); ++i) {
        double s = 0.0;
        for (const auto& [j, v] : A.rows[i]) s += v * x[static_cast<std::size_t>(j)];
        y[i] = s;
    }
    return y;
}

/** @brief Mesh and the boundary edges, built once per run. */
struct Edge {
    int a = 0;                     ///< First node.
    int b = 0;                     ///< Second node.
    double length = 0.0;           ///< Edge length.
    const Boundary* bc = nullptr;  ///< Condition on this edge.
};

struct Mesh {
    int nxn = 0;                   ///< Nodes along x.
    int nyn = 0;                   ///< Nodes along y.
    int nx = 0;                    ///< Elements along x.
    int ny = 0;                    ///< Elements along y.
    double hx = 0.0;               ///< Element size along x.
    double hy = 0.0;               ///< Element size along y.
    std::vector<char> elemActive;  ///< Element (ei, ej) is solid: index ej * nx + ei.
    std::vector<char> nodeActive;  ///< Node belongs to at least one solid element.
    std::vector<Edge> edges;       ///< Plate sides and hole boundaries that carry a condition.
    std::string error;

    int id(int i, int j) const { return j * nxn + i; }
    int elem(int i, int j) const { return j * nx + i; }
};

/** @brief True when the point is inside one of the holes. */
bool insideHole(const Hole& h, double x, double y) {
    if (h.shape == HoleShape::Circle) {
        const double dx = x - h.cx;
        const double dy = y - h.cy;
        return dx * dx + dy * dy < h.r * h.r;
    }
    return x >= h.x0 && x <= h.x1 && y >= h.y0 && y <= h.y1;
}

/** @brief Builds the element activity, the active nodes and the edges with a condition. */
Mesh buildMesh(const Model& m) {
    Mesh mesh;
    if (!(m.lx > 0.0) || !(m.ly > 0.0) || m.nx < 1 || m.ny < 1) {
        mesh.error = "plate size and element counts must be positive";
        return mesh;
    }
    mesh.nx = m.nx;
    mesh.ny = m.ny;
    mesh.nxn = m.nx + 1;
    mesh.nyn = m.ny + 1;
    mesh.hx = m.lx / m.nx;
    mesh.hy = m.ly / m.ny;

    mesh.elemActive.assign(static_cast<std::size_t>(m.nx) * m.ny, 0);
    int activeCount = 0;
    for (int ej = 0; ej < m.ny; ++ej) {
        for (int ei = 0; ei < m.nx; ++ei) {
            const double xc = (ei + 0.5) * mesh.hx;
            const double yc = (ej + 0.5) * mesh.hy;
            bool solid = true;
            for (const Hole& h : m.holes) {
                if (insideHole(h, xc, yc)) {
                    solid = false;
                    break;
                }
            }
            mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei, ej))] = solid ? 1 : 0;
            if (solid) ++activeCount;
        }
    }
    if (activeCount == 0) {
        mesh.error = "no solid element: the holes cover the whole plate";
        return mesh;
    }

    mesh.nodeActive.assign(static_cast<std::size_t>(mesh.nxn) * mesh.nyn, 0);
    for (int ej = 0; ej < m.ny; ++ej) {
        for (int ei = 0; ei < m.nx; ++ei) {
            if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei, ej))]) continue;
            mesh.nodeActive[static_cast<std::size_t>(mesh.id(ei, ej))] = 1;
            mesh.nodeActive[static_cast<std::size_t>(mesh.id(ei + 1, ej))] = 1;
            mesh.nodeActive[static_cast<std::size_t>(mesh.id(ei + 1, ej + 1))] = 1;
            mesh.nodeActive[static_cast<std::size_t>(mesh.id(ei, ej + 1))] = 1;
        }
    }

    // Each solid element contributes its sides: a side on the plate boundary takes the plate condition, a side
    // between a solid element and a hole takes the hole condition. Sides between two solid elements carry nothing.
    for (int ej = 0; ej < m.ny; ++ej) {
        for (int ei = 0; ei < m.nx; ++ei) {
            if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei, ej))]) continue;

            // bottom: neighbour (ei, ej - 1)
            if (ej == 0) {
                mesh.edges.push_back({mesh.id(ei, ej), mesh.id(ei + 1, ej), mesh.hx, &m.bottom});
            } else if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei, ej - 1))]) {
                mesh.edges.push_back({mesh.id(ei, ej), mesh.id(ei + 1, ej), mesh.hx, &m.hole});
            }
            // top: neighbour (ei, ej + 1)
            if (ej + 1 == m.ny) {
                mesh.edges.push_back({mesh.id(ei, ej + 1), mesh.id(ei + 1, ej + 1), mesh.hx, &m.top});
            } else if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei, ej + 1))]) {
                mesh.edges.push_back({mesh.id(ei, ej + 1), mesh.id(ei + 1, ej + 1), mesh.hx, &m.hole});
            }
            // left: neighbour (ei - 1, ej)
            if (ei == 0) {
                mesh.edges.push_back({mesh.id(ei, ej), mesh.id(ei, ej + 1), mesh.hy, &m.left});
            } else if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei - 1, ej))]) {
                mesh.edges.push_back({mesh.id(ei, ej), mesh.id(ei, ej + 1), mesh.hy, &m.hole});
            }
            // right: neighbour (ei + 1, ej)
            if (ei + 1 == m.nx) {
                mesh.edges.push_back({mesh.id(ei + 1, ej), mesh.id(ei + 1, ej + 1), mesh.hy, &m.right});
            } else if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei + 1, ej))]) {
                mesh.edges.push_back({mesh.id(ei + 1, ej), mesh.id(ei + 1, ej + 1), mesh.hy, &m.hole});
            }
        }
    }
    return mesh;
}

/** @brief Node position of node id. */
void nodePosition(const Mesh& mesh, int id, double& x, double& y) {
    x = (id % mesh.nxn) * mesh.hx;
    y = (id / mesh.nxn) * mesh.hy;
}

/**
 * @brief Conduction matrix K(T) and the load vector g(T, t) of the solid, with the edge terms.
 * @return false and sets err when a material value is not positive.
 */
bool assembleSystem(const Model& m, const Mesh& mesh, const std::vector<double>& T, double t, Sparse& K,
                    std::vector<double>& g, std::string& err) {
    const std::size_t n = static_cast<std::size_t>(mesh.nxn) * mesh.nyn;
    K = Sparse(n);
    g.assign(n, 0.0);

    // Shape functions of the bilinear quad, nodes in the order (-1,-1), (1,-1), (1,1), (-1,1).
    const double sx[4] = {-1.0, 1.0, 1.0, -1.0};
    const double sy[4] = {-1.0, -1.0, 1.0, 1.0};

    for (int ej = 0; ej < mesh.ny; ++ej) {
        for (int ei = 0; ei < mesh.nx; ++ei) {
            if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei, ej))]) continue;
            const int nodes[4] = {mesh.id(ei, ej), mesh.id(ei + 1, ej), mesh.id(ei + 1, ej + 1), mesh.id(ei, ej + 1)};
            const double x0 = ei * mesh.hx;
            const double y0 = ej * mesh.hy;
            const double detJ = mesh.hx * mesh.hy / 4.0;

            for (double xi : {-kGaussPoint, kGaussPoint}) {
                for (double eta : {-kGaussPoint, kGaussPoint}) {
                    double N[4];
                    double dNx[4];
                    double dNy[4];
                    for (int a = 0; a < 4; ++a) {
                        N[a] = 0.25 * (1.0 + sx[a] * xi) * (1.0 + sy[a] * eta);
                        dNx[a] = 0.25 * sx[a] * (1.0 + sy[a] * eta) * (2.0 / mesh.hx);
                        dNy[a] = 0.25 * sy[a] * (1.0 + sx[a] * xi) * (2.0 / mesh.hy);
                    }
                    const double x = x0 + (xi + 1.0) * 0.5 * mesh.hx;
                    const double y = y0 + (eta + 1.0) * 0.5 * mesh.hy;
                    double Tgp = 0.0;
                    for (int a = 0; a < 4; ++a) Tgp += N[a] * T[static_cast<std::size_t>(nodes[a])];

                    const double k = m.conductivity(x, y, Tgp);
                    if (!(k > 0.0)) {
                        err = "conductivity must be positive";
                        return false;
                    }
                    const double q = m.source ? m.source(x, y, t) : 0.0;
                    const double w = m.thickness * detJ;

                    for (int a = 0; a < 4; ++a) {
                        for (int b = 0; b < 4; ++b) {
                            K.add(nodes[a], nodes[b], w * k * (dNx[a] * dNx[b] + dNy[a] * dNy[b]));
                        }
                        g[static_cast<std::size_t>(nodes[a])] += w * q * N[a];
                    }
                }
            }
        }
    }

    // Edge terms. A temperature side adds nothing here (it is imposed on the system); a flux side adds the load; a
    // convection-radiation side adds a(T) (T - T_inf) with a(T) = h + eps sigma (T^2 + Tinf^2)(T + Tinf).
    const double s = kGaussPoint;
    for (const Edge& e : mesh.edges) {
        const Boundary& bc = *e.bc;
        if (bc.kind == BoundaryKind::Temperature) continue;
        const double J = e.length / 2.0;
        const double Ta = T[static_cast<std::size_t>(e.a)];
        const double Tb = T[static_cast<std::size_t>(e.b)];

        if (bc.kind == BoundaryKind::HeatFlux) {
            if (!bc.value) continue;
            const double qv = bc.value(t);
            for (double sg : {-s, s}) {
                const double Na = 0.5 * (1.0 - sg);
                const double Nb = 0.5 * (1.0 + sg);
                g[static_cast<std::size_t>(e.a)] += m.thickness * qv * Na * J;
                g[static_cast<std::size_t>(e.b)] += m.thickness * qv * Nb * J;
            }
            continue;
        }

        // Convection
        const double Tinf = bc.ambient ? bc.ambient(t) : 0.0;
        for (double sg : {-s, s}) {
            const double Na = 0.5 * (1.0 - sg);
            const double Nb = 0.5 * (1.0 + sg);
            const double Tgp = Na * Ta + Nb * Tb;
            const double a = bc.h + bc.emissivity * m.stefanBoltzmann * (Tgp * Tgp + Tinf * Tinf) * (Tgp + Tinf);
            const double w = m.thickness * a * J;
            K.add(e.a, e.a, w * Na * Na);
            K.add(e.a, e.b, w * Na * Nb);
            K.add(e.b, e.a, w * Nb * Na);
            K.add(e.b, e.b, w * Nb * Nb);
            g[static_cast<std::size_t>(e.a)] += m.thickness * a * Tinf * Na * J;
            g[static_cast<std::size_t>(e.b)] += m.thickness * a * Tinf * Nb * J;
        }
    }
    return true;
}

/** @brief Consistent capacity matrix, or its row-sum lumping. */
bool assembleCapacity(const Model& m, const Mesh& mesh, Capacity kind, Sparse& C, std::string& err) {
    const std::size_t n = static_cast<std::size_t>(mesh.nxn) * mesh.nyn;
    Sparse consistent(n);
    const double sx[4] = {-1.0, 1.0, 1.0, -1.0};
    const double sy[4] = {-1.0, -1.0, 1.0, 1.0};

    for (int ej = 0; ej < mesh.ny; ++ej) {
        for (int ei = 0; ei < mesh.nx; ++ei) {
            if (!mesh.elemActive[static_cast<std::size_t>(mesh.elem(ei, ej))]) continue;
            const int nodes[4] = {mesh.id(ei, ej), mesh.id(ei + 1, ej), mesh.id(ei + 1, ej + 1), mesh.id(ei, ej + 1)};
            const double x0 = ei * mesh.hx;
            const double y0 = ej * mesh.hy;
            const double detJ = mesh.hx * mesh.hy / 4.0;
            for (double xi : {-kGaussPoint, kGaussPoint}) {
                for (double eta : {-kGaussPoint, kGaussPoint}) {
                    double N[4];
                    for (int a = 0; a < 4; ++a) N[a] = 0.25 * (1.0 + sx[a] * xi) * (1.0 + sy[a] * eta);
                    const double x = x0 + (xi + 1.0) * 0.5 * mesh.hx;
                    const double y = y0 + (eta + 1.0) * 0.5 * mesh.hy;
                    const double rho = m.volumetricHeatCapacity(x, y);
                    if (!(rho > 0.0)) {
                        err = "volumetric heat capacity must be positive";
                        return false;
                    }
                    const double w = m.thickness * rho * detJ;
                    for (int a = 0; a < 4; ++a)
                        for (int b = 0; b < 4; ++b) consistent.add(nodes[a], nodes[b], w * N[a] * N[b]);
                }
            }
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

/**
 * @brief Solves A x = b for the symmetric positive-definite matrix, after the fixed nodes are imposed.
 *
 * The fixed rows become identity rows; the columns of the fixed nodes move to the right-hand side so that the system
 * stays symmetric. The solve is conjugate gradient with a Jacobi preconditioner.
 */
bool solveFixed(Sparse A, std::vector<double> b, const std::vector<char>& fixed, const std::vector<double>& fixedValue,
                std::vector<double>& x, std::string& err) {
    const std::size_t n = A.rows.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (fixed[i]) continue;
        for (auto it = A.rows[i].begin(); it != A.rows[i].end();) {
            const std::size_t j = static_cast<std::size_t>(it->first);
            if (fixed[j]) {
                b[i] -= it->second * fixedValue[j];
                it = A.rows[i].erase(it);
            } else {
                ++it;
            }
        }
    }
    std::vector<double> diag(n, 1.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (fixed[i]) {
            A.rows[i].clear();
            A.rows[i][static_cast<int>(i)] = 1.0;
            b[i] = fixedValue[i];
        }
        const auto it = A.rows[i].find(static_cast<int>(i));
        if (it == A.rows[i].end() || !(it->second > 0.0)) {
            err = "the linear system is singular: the temperature level is not fixed";
            return false;
        }
        diag[i] = it->second;
    }

    // Conjugate gradient with Jacobi preconditioner, starting from x.
    x.resize(n, 0.0);
    std::vector<double> Ax = multiply(A, x);
    std::vector<double> r(n);
    double bnorm = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        r[i] = b[i] - Ax[i];
        bnorm += b[i] * b[i];
    }
    bnorm = std::sqrt(bnorm);
    if (bnorm == 0.0) bnorm = 1.0;

    std::vector<double> z(n);
    for (std::size_t i = 0; i < n; ++i) z[i] = r[i] / diag[i];
    std::vector<double> p = z;
    double rz = 0.0;
    for (std::size_t i = 0; i < n; ++i) rz += r[i] * z[i];

    const int maxIterations = static_cast<int>(20 * n + 1000);
    for (int it = 0; it < maxIterations; ++it) {
        double rnorm = 0.0;
        for (std::size_t i = 0; i < n; ++i) rnorm += r[i] * r[i];
        if (std::sqrt(rnorm) <= 1e-13 * bnorm) return true;

        const std::vector<double> Ap = multiply(A, p);
        double pAp = 0.0;
        for (std::size_t i = 0; i < n; ++i) pAp += p[i] * Ap[i];
        if (!(pAp > 0.0)) {
            err = "the linear system is not positive definite";
            return false;
        }
        const double alpha = rz / pAp;
        for (std::size_t i = 0; i < n; ++i) {
            x[i] += alpha * p[i];
            r[i] -= alpha * Ap[i];
        }
        for (std::size_t i = 0; i < n; ++i) z[i] = r[i] / diag[i];
        double rzNew = 0.0;
        for (std::size_t i = 0; i < n; ++i) rzNew += r[i] * z[i];
        const double beta = rzNew / rz;
        rz = rzNew;
        for (std::size_t i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    }
    err = "the linear solver did not converge";
    return false;
}

/** @brief Fixed (Dirichlet) nodes at time t: temperature sides and inactive nodes. */
void fixedNodes(const Model& m, const Mesh& mesh, double t, std::vector<char>& fixed, std::vector<double>& value) {
    const std::size_t n = static_cast<std::size_t>(mesh.nxn) * mesh.nyn;
    fixed.assign(n, 0);
    value.assign(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        if (!mesh.nodeActive[i]) fixed[i] = 1;
    }
    for (const Edge& e : mesh.edges) {
        if (e.bc->kind != BoundaryKind::Temperature) continue;
        const double v = e.bc->value(t);
        for (int node : {e.a, e.b}) {
            fixed[static_cast<std::size_t>(node)] = 1;
            value[static_cast<std::size_t>(node)] = v;
        }
    }
    (void)m;
}

/** @brief Checks one boundary condition. */
std::string checkBoundary(const Boundary& b, const char* name) {
    std::ostringstream os;
    if (b.kind == BoundaryKind::Temperature && !b.value) {
        os << name << ": a temperature side needs a temperature value";
        return os.str();
    }
    if (b.kind == BoundaryKind::Convection) {
        if (!b.ambient) {
            os << name << ": a convection side needs an ambient temperature";
            return os.str();
        }
        if (!(b.h >= 0.0)) {
            os << name << ": the convection coefficient must not be negative";
            return os.str();
        }
        if (!(b.emissivity >= 0.0 && b.emissivity <= 1.0)) {
            os << name << ": the emissivity must be in [0, 1]";
            return os.str();
        }
    }
    return "";
}

} // namespace

std::string validate(const Model& m, const Options& o) {
    if (!(m.lx > 0.0) || !(m.ly > 0.0)) return "lx and ly must be positive";
    if (m.nx < 1 || m.ny < 1) return "nx and ny must be at least 1";
    if (!(m.thickness > 0.0)) return "thickness must be positive";
    if (!m.conductivity) return "conductivity is required";
    if (!o.steady) {
        if (!m.volumetricHeatCapacity) return "volumetric heat capacity is required for a transient run";
        if (!(o.timeStep > 0.0)) return "time step must be positive";
        if (!(o.endTime > 0.0)) return "end time must be positive";
        if (!(o.theta >= 0.0 && o.theta <= 1.0)) return "theta must be in [0, 1]";
        if (o.recordEvery < 1) return "recordEvery must be at least 1";
    }
    if (!(o.tolerance > 0.0)) return "tolerance must be positive";
    if (o.maxIterations < 1) return "maxIterations must be at least 1";

    for (const Hole& h : m.holes) {
        if (h.shape == HoleShape::Circle) {
            if (!(h.r > 0.0)) return "hole radius must be positive";
            if (h.cx - h.r < 0.0 || h.cx + h.r > m.lx || h.cy - h.r < 0.0 || h.cy + h.r > m.ly)
                return "a circular hole must lie fully inside the plate";
        } else {
            if (!(h.x0 < h.x1) || !(h.y0 < h.y1)) return "rectangular hole needs x0 < x1 and y0 < y1";
            if (h.x0 < 0.0 || h.x1 > m.lx || h.y0 < 0.0 || h.y1 > m.ly)
                return "a rectangular hole must lie fully inside the plate";
        }
    }

    std::string err = checkBoundary(m.left, "left");
    if (err.empty()) err = checkBoundary(m.right, "right");
    if (err.empty()) err = checkBoundary(m.bottom, "bottom");
    if (err.empty()) err = checkBoundary(m.top, "top");
    if (err.empty()) err = checkBoundary(m.hole, "hole");
    if (!err.empty()) return err;

    const Mesh mesh = buildMesh(m);
    if (!mesh.error.empty()) return mesh.error;

    bool anchored = false;
    std::map<int, double> cornerValue;
    for (const Edge& e : mesh.edges) {
        if (e.bc->kind == BoundaryKind::Convection) anchored = true;
        if (e.bc->kind != BoundaryKind::Temperature) continue;
        anchored = true;
        const double v = e.bc->value(0.0);
        for (int node : {e.a, e.b}) {
            const auto it = cornerValue.find(node);
            if (it != cornerValue.end() && std::abs(it->second - v) > 1e-12) {
                return "two temperature sides meet at a node with different temperatures";
            }
            cornerValue[node] = v;
        }
    }
    if (o.steady && !anchored) {
        return "a steady problem needs a temperature side or a convection side: the temperature level is undetermined";
    }
    return "";
}

Result solve(const Model& m, const Options& o) {
    Result res;
    res.error = validate(m, o);
    if (!res.error.empty()) return res;

    const Mesh mesh = buildMesh(m);
    const std::size_t n = static_cast<std::size_t>(mesh.nxn) * mesh.nyn;
    res.nodesX = mesh.nxn;
    res.nodesY = mesh.nyn;
    res.active.assign(n, false);
    for (std::size_t i = 0; i < n; ++i) res.active[i] = mesh.nodeActive[i] != 0;

    std::vector<double> T(n, 0.0);
    if (m.initialTemperature) {
        for (std::size_t i = 0; i < n; ++i) {
            if (!mesh.nodeActive[i]) continue;
            double x = 0.0;
            double y = 0.0;
            nodePosition(mesh, static_cast<int>(i), x, y);
            T[i] = m.initialTemperature(x, y);
        }
    }

    std::string err;
    Sparse C;
    if (!o.steady && !assembleCapacity(m, mesh, o.capacity, C, err)) {
        res.error = err;
        return res;
    }

    // Picard iteration on one (theta) step: the coefficients are frozen at the iterate, the system is solved, the
    // iterate is updated until the change is below the tolerance.
    const auto maxAbs = [](const std::vector<double>& v) {
        double mx = 0.0;
        for (double x : v) mx = std::max(mx, std::abs(x));
        return mx;
    };

    if (o.steady) {
        std::vector<char> fixed;
        std::vector<double> fixedValue;
        fixedNodes(m, mesh, 0.0, fixed, fixedValue);
        std::vector<double> Tk = T;
        bool converged = false;
        int its = 0;
        for (int it = 1; it <= o.maxIterations; ++it) {
            Sparse K;
            std::vector<double> g;
            if (!assembleSystem(m, mesh, Tk, 0.0, K, g, err) ||
                !solveFixed(K, g, fixed, fixedValue, T, err)) {
                res.error = err;
                return res;
            }
            double change = 0.0;
            for (std::size_t i = 0; i < n; ++i) change = std::max(change, std::abs(T[i] - Tk[i]));
            Tk = T;
            its = it;
            if (change <= o.tolerance * std::max(1.0, maxAbs(Tk))) {
                converged = true;
                break;
            }
        }
        res.maxPicardIterations = its;
        res.converged = converged;
        if (!converged) {
            res.error = "Picard iteration did not converge";
            return res;
        }
        res.times = {0.0};
        res.temperatures = {T};
        return res;
    }

    const double theta = o.theta;
    const int steps = std::max(1, static_cast<int>(std::ceil(o.endTime / o.timeStep - 1e-9)));
    const double dt = o.endTime / steps;

    res.times = {0.0};
    res.temperatures = {T};
    bool allConverged = true;

    for (int step = 0; step < steps; ++step) {
        const double tn = step * dt;
        const double tnp1 = (step + 1) * dt;

        // Explicit part at T^n and t_n.
        Sparse Kn;
        std::vector<double> gn;
        if (!assembleSystem(m, mesh, T, tn, Kn, gn, err)) {
            res.error = err;
            return res;
        }
        const std::vector<double> CT = multiply(C, T);
        const std::vector<double> KT = multiply(Kn, T);

        std::vector<char> fixed;
        std::vector<double> fixedValue;
        fixedNodes(m, mesh, tnp1, fixed, fixedValue);

        std::vector<double> Tk = T;
        bool converged = false;
        int its = 0;
        for (int it = 1; it <= o.maxIterations; ++it) {
            Sparse Kk;
            std::vector<double> gk;
            if (!assembleSystem(m, mesh, Tk, tnp1, Kk, gk, err)) {
                res.error = err;
                return res;
            }
            // (C / dt + theta K^k) T^{k+1} = (C / dt) T^n - (1 - theta) K^n T^n + (1 - theta) g^n + theta g^k
            const Sparse A = combine(C, 1.0 / dt, Kk, theta);
            std::vector<double> b(n);
            for (std::size_t i = 0; i < n; ++i) {
                b[i] = CT[i] / dt - (1.0 - theta) * KT[i] + (1.0 - theta) * gn[i] + theta * gk[i];
            }
            std::vector<double> Tnew = Tk;
            if (!solveFixed(A, b, fixed, fixedValue, Tnew, err)) {
                res.error = err;
                return res;
            }
            double change = 0.0;
            for (std::size_t i = 0; i < n; ++i) change = std::max(change, std::abs(Tnew[i] - Tk[i]));
            Tk = Tnew;
            its = it;
            if (change <= o.tolerance * std::max(1.0, maxAbs(Tk))) {
                converged = true;
                break;
            }
        }
        res.maxPicardIterations = std::max(res.maxPicardIterations, its);
        if (!converged) {
            allConverged = false;
            res.error = "Picard iteration did not converge";
            res.converged = false;
            return res;
        }
        T = Tk;

        const int done = step + 1;
        if (done % o.recordEvery == 0 || done == steps) {
            res.times.push_back(tnp1);
            res.temperatures.push_back(T);
        }
    }
    res.converged = allConverged;
    return res;
}

} // namespace OpenModel::Heat2D
