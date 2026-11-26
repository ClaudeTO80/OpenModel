#include <OpenModel/Heat1D.hpp>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

/**
 * @file Heat1D.cpp
 * @brief Implementation of the one-dimensional heat conduction solver (see Heat1D.hpp and docs/HEAT_1D.md).
 *
 * Each step solves (C/dt + theta K(T^k)) T^{k+1} = (C/dt - (1 - theta) K(T^n)) T^n + (1 - theta) g(T^n, t^n)
 *                                              + theta g(T^k, t^{n+1})
 * with Dirichlet ends imposed on the system. The coefficients that depend on T are frozen at the iterate T^k and the
 * solve is repeated until the iterate stops changing. At convergence the frozen coefficients equal the exact ones, so
 * the scheme is the theta method applied to the complete non-linear problem.
 */

namespace OpenModel::Heat1D {
namespace {

/** @brief Abscissa of the two-point Gauss rule on [-1, 1] (weights are 1). */
constexpr double kGaussAbscissa = 0.57735026918962576;

/**
 * @brief Tridiagonal matrix stored by its three diagonals.
 *
 * `lower[i]` is A(i, i-1) (unused for i = 0), `diag[i]` is A(i, i), `upper[i]` is A(i, i+1) (unused for i = n-1).
 */
struct Tridiagonal {
    std::vector<double> lower;
    std::vector<double> diag;
    std::vector<double> upper;

    explicit Tridiagonal(std::size_t n = 0) : lower(n, 0.0), diag(n, 0.0), upper(n, 0.0) {}

    /** @brief Number of rows (= columns). */
    std::size_t size() const { return diag.size(); }

    /** @brief Adds `weight` times `other` to this matrix (same size). */
    void add(const Tridiagonal& other, double weight) {
        for (std::size_t i = 0; i < size(); ++i) {
            diag[i] += weight * other.diag[i];
            lower[i] += weight * other.lower[i];
            upper[i] += weight * other.upper[i];
        }
    }

    /** @brief Matrix-vector product. */
    std::vector<double> multiply(const std::vector<double>& v) const {
        const std::size_t n = size();
        std::vector<double> out(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) {
            double s = diag[i] * v[i];
            if (i > 0) s += lower[i] * v[i - 1];
            if (i + 1 < n) s += upper[i] * v[i + 1];
            out[i] = s;
        }
        return out;
    }
};

/** @brief Mesh: node positions and the (uniform) element length. */
struct Discretization {
    std::vector<double> x;
    double h = 0.0;
};

/** @brief Frozen-coefficient system: stiffness-like matrix K and load vector g of the semi-discrete equation. */
struct Linearised {
    Tridiagonal K;
    std::vector<double> g;
};

/**
 * @brief Adds the element matrix [[m11, m12], [m12, m22]] of element e (nodes e and e+1).
 */
void addElementPair(Tridiagonal& M, std::size_t e, double m11, double m12, double m22) {
    M.diag[e] += m11;
    M.upper[e] += m12;
    M.lower[e + 1] += m12;
    M.diag[e + 1] += m22;
}

/**
 * @brief Radiative exchange coefficient eps * sigma * (T^2 + Ta^2) * (T + Ta), so that eps * sigma * (T^4 - Ta^4) equals
 *        this coefficient times (T - Ta). Zero when there is no radiation.
 */
double radiationCoefficient(double emissivity, double T, double ambient, double sigma) {
    if (emissivity <= 0.0) return 0.0;
    return emissivity * sigma * (T * T + ambient * ambient) * (T + ambient);
}

/** @brief Ambient temperature of a function, or 0 when it is not set (inactive exchange). */
double evaluate(const TimeFn& fn, double t) { return fn ? fn(t) : 0.0; }

/**
 * @brief Frozen-coefficient assembly at the temperatures T and time t.
 *
 * Conduction: K_e = int k(x,T) A(x) B^T B dx, with B = dN/dx. Lateral exchange and end convection share one form: the
 * exchange flux is a(T) (T - T_inf), with a = h + radiation coefficient frozen at T; it adds a(T) to K and a(T) T_inf to g.
 * Heat flux at an end adds A q'' to g. The source adds int A q N dx to g.
 */
Linearised assemble(const Model& m, const Discretization& d, const std::vector<double>& T, double t) {
    const std::size_t n = d.x.size();
    const std::size_t ne = n - 1;
    const double h = d.h;
    const double sigma = m.stefanBoltzmann;
    const double w = 0.5 * h; // Gauss weight 1 times the Jacobian h/2

    Linearised out{Tridiagonal(n), std::vector<double>(n, 0.0)};

    for (std::size_t e = 0; e < ne; ++e) {
        const double x1 = d.x[e];
        for (int ig = 0; ig < 2; ++ig) {
            const double xi = ig == 0 ? -kGaussAbscissa : kGaussAbscissa;
            const double N1 = 0.5 * (1.0 - xi);
            const double N2 = 0.5 * (1.0 + xi);
            const double x = x1 + N2 * h;
            const double Tg = N1 * T[e] + N2 * T[e + 1];
            const double A = m.area(x);

            // Conduction: w * kA * (dN/dx)_i (dN/dx)_j with dN1/dx = -1/h, dN2/dx = 1/h.
            const double kA = w * m.conductivity(x, Tg) * A;
            addElementPair(out.K, e, kA / (h * h), -kA / (h * h), kA / (h * h));

            // Lateral exchange along the side (P per unit length).
            if (m.lateral.perimeter) {
                const double P = m.lateral.perimeter(x);
                const double Ta = evaluate(m.lateral.ambient, t);
                const double a = m.lateral.h + radiationCoefficient(m.lateral.emissivity, Tg, Ta, sigma);
                const double wa = w * P * a;
                addElementPair(out.K, e, wa * N1 * N1, wa * N1 * N2, wa * N2 * N2);
                out.g[e] += wa * Ta * N1;
                out.g[e + 1] += wa * Ta * N2;
            }

            // Volumetric source.
            if (m.source) {
                const double f = w * A * m.source(x, t);
                out.g[e] += f * N1;
                out.g[e + 1] += f * N2;
            }
        }
    }

    // Ends: heat flux and convection-radiation (the area at the end node weights the flux).
    const auto endCondition = [&](const Boundary& b, std::size_t node) {
        const double A = m.area(d.x[node]);
        switch (b.kind) {
        case BoundaryKind::Temperature:
            break; // imposed on the system, see fixedValues()
        case BoundaryKind::HeatFlux:
            if (b.value) out.g[node] += A * b.value(t);
            break;
        case BoundaryKind::Convection: {
            const double Ta = evaluate(b.ambient, t);
            const double a = b.h + radiationCoefficient(b.emissivity, T[node], Ta, sigma);
            out.K.diag[node] += A * a;
            out.g[node] += A * a * Ta;
            break;
        }
        }
    };
    endCondition(m.left, 0);
    endCondition(m.right, n - 1);

    return out;
}

/** @brief Capacity matrix C = int rho c_p A N_i N_j dx (consistent) or its row-sum lumping (lumped). */
Tridiagonal capacityMatrix(const Model& m, const Discretization& d, Capacity mode) {
    const std::size_t n = d.x.size();
    const std::size_t ne = n - 1;
    const double w = 0.5 * d.h;
    Tridiagonal C(n);
    for (std::size_t e = 0; e < ne; ++e) {
        for (int ig = 0; ig < 2; ++ig) {
            const double xi = ig == 0 ? -kGaussAbscissa : kGaussAbscissa;
            const double N1 = 0.5 * (1.0 - xi);
            const double N2 = 0.5 * (1.0 + xi);
            const double x = d.x[e] + N2 * d.h;
            const double c = w * m.volumetricHeatCapacity(x) * m.area(x);
            if (mode == Capacity::Consistent) {
                addElementPair(C, e, c * N1 * N1, c * N1 * N2, c * N2 * N2);
            } else {
                C.diag[e] += c * N1;
                C.diag[e + 1] += c * N2;
            }
        }
    }
    return C;
}

/** @brief Nodes whose temperature is imposed, with the value at time t. */
std::vector<std::pair<std::size_t, double>> fixedValues(const Model& m, std::size_t n, double t) {
    std::vector<std::pair<std::size_t, double>> fixed;
    if (m.left.kind == BoundaryKind::Temperature) fixed.emplace_back(0, m.left.value(t));
    if (m.right.kind == BoundaryKind::Temperature) fixed.emplace_back(n - 1, m.right.value(t));
    return fixed;
}

/**
 * @brief Imposes T[node] = value on a tridiagonal system (the node must be an end: 0 or n-1).
 *
 * The row becomes the identity; the coupling of the neighbour row to the known value moves to its right-hand side.
 */
void imposeDirichlet(Tridiagonal& A, std::vector<double>& rhs, std::size_t node, double value) {
    const std::size_t n = A.size();
    if (node > 0) {
        rhs[node - 1] -= A.upper[node - 1] * value;
        A.upper[node - 1] = 0.0;
    }
    if (node + 1 < n) {
        rhs[node + 1] -= A.lower[node + 1] * value;
        A.lower[node + 1] = 0.0;
    }
    A.diag[node] = 1.0;
    A.lower[node] = 0.0;
    A.upper[node] = 0.0;
    rhs[node] = value;
}

/** @brief Thomas algorithm for a tridiagonal system (no pivoting: the systems here are symmetric positive definite). */
std::vector<double> solveTridiagonal(const Tridiagonal& A, std::vector<double> rhs) {
    const std::size_t n = A.size();
    std::vector<double> c(n, 0.0);
    std::vector<double> d = std::move(rhs);

    double denom = A.diag[0];
    if (denom == 0.0) throw std::runtime_error("singular system in the heat solver");
    c[0] = A.upper[0] / denom;
    d[0] /= denom;
    for (std::size_t i = 1; i < n; ++i) {
        denom = A.diag[i] - A.lower[i] * c[i - 1];
        if (denom == 0.0) throw std::runtime_error("singular system in the heat solver");
        c[i] = i + 1 < n ? A.upper[i] / denom : 0.0;
        d[i] = (d[i] - A.lower[i] * d[i - 1]) / denom;
    }

    std::vector<double> x(n, 0.0);
    x[n - 1] = d[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) x[i] = d[i] - c[i] * x[i + 1];
    return x;
}

/**
 * @brief One time step (or the steady problem when C is null) by Picard iteration.
 * @param Tn          temperatures at the start of the step (time tn)
 * @param dt          step length (unused when C is null)
 * @param iterations  receives the number of Picard iterations used
 * @param converged   receives true when the iterate reached the tolerance
 */
std::vector<double> picardStep(const Model& m, const Discretization& d, const Options& o, const std::vector<double>& Tn,
                               double tn, double dt, const Tridiagonal* C, int& iterations, bool& converged) {
    const std::size_t n = Tn.size();
    const double theta = o.theta;
    const double tnew = tn + dt;
    const double thetaNew = C ? theta : 1.0;

    // Old-level part: rhsBase = (C/dt - (1 - theta) K^n) T^n + (1 - theta) g^n. Zero for a steady run.
    std::vector<double> rhsBase(n, 0.0);
    Tridiagonal Cdt(n);
    if (C) {
        Cdt.add(*C, 1.0 / dt);
        const Linearised old = assemble(m, d, Tn, tn);
        const std::vector<double> cTn = Cdt.multiply(Tn);
        const std::vector<double> kTn = old.K.multiply(Tn);
        for (std::size_t i = 0; i < n; ++i) rhsBase[i] = cTn[i] - (1.0 - theta) * kTn[i] + (1.0 - theta) * old.g[i];
    }

    std::vector<double> Tk = Tn;
    converged = false;
    iterations = 0;
    for (int it = 1; it <= o.maxIterations; ++it) {
        const Linearised cur = assemble(m, d, Tk, tnew);
        Tridiagonal A = C ? Cdt : Tridiagonal(n); // C/dt, or an empty matrix for the steady problem
        A.add(cur.K, thetaNew);
        std::vector<double> rhs(n, 0.0);
        for (std::size_t i = 0; i < n; ++i) rhs[i] = rhsBase[i] + thetaNew * cur.g[i];
        for (const auto& [node, value] : fixedValues(m, n, tnew)) imposeDirichlet(A, rhs, node, value);

        const std::vector<double> next = solveTridiagonal(A, rhs);
        iterations = it;

        double change = 0.0;
        double scale = 1.0;
        for (std::size_t i = 0; i < n; ++i) {
            change = std::max(change, std::fabs(next[i] - Tk[i]));
            scale = std::max(scale, std::fabs(next[i]));
        }
        Tk = next;
        if (change <= o.tolerance * scale) {
            converged = true;
            break;
        }
    }
    return Tk;
}

/** @brief Checks one end condition; returns an empty string when it is usable. */
std::string validateBoundary(const Boundary& b, const std::string& name) {
    switch (b.kind) {
    case BoundaryKind::Temperature:
        if (!b.value) return name + ": a temperature end needs a value";
        break;
    case BoundaryKind::HeatFlux:
        break;
    case BoundaryKind::Convection:
        if (!(b.h >= 0.0)) return name + ": convection coefficient must be >= 0";
        if (!(b.emissivity >= 0.0 && b.emissivity <= 1.0)) return name + ": emissivity must be in [0, 1]";
        if (!b.ambient) return name + ": a convection end needs an ambient temperature";
        break;
    }
    return {};
}

} // namespace

std::string validate(const Model& m, const Options& o) {
    if (!(m.length > 0.0)) return "length must be positive";
    if (m.elements < 1) return "at least one element is needed";
    if (!m.conductivity) return "conductivity k(x, T) is required";
    if (!m.area) return "cross-section A(x) is required";
    if (!o.steady && !m.volumetricHeatCapacity) return "volumetric heat capacity is required for a transient run";

    // Sample the properties at the element midpoints, at the initial temperature.
    const double h = m.length / m.elements;
    for (int e = 0; e < m.elements; ++e) {
        const double x = (e + 0.5) * h;
        const double T0 = m.initialTemperature ? m.initialTemperature(x) : 0.0;
        if (!(m.conductivity(x, T0) > 0.0)) return "conductivity must be positive (at x = " + std::to_string(x) + ")";
        if (!(m.area(x) > 0.0)) return "cross-section must be positive (at x = " + std::to_string(x) + ")";
        if (!o.steady && !(m.volumetricHeatCapacity(x) > 0.0))
            return "volumetric heat capacity must be positive (at x = " + std::to_string(x) + ")";
        if (m.lateral.perimeter && !(m.lateral.perimeter(x) >= 0.0))
            return "perimeter must be >= 0 (at x = " + std::to_string(x) + ")";
    }

    if (auto err = validateBoundary(m.left, "left end"); !err.empty()) return err;
    if (auto err = validateBoundary(m.right, "right end"); !err.empty()) return err;

    const bool lateralActive = m.lateral.h > 0.0 || m.lateral.emissivity > 0.0;
    if (!(m.lateral.h >= 0.0)) return "lateral convection coefficient must be >= 0";
    if (!(m.lateral.emissivity >= 0.0 && m.lateral.emissivity <= 1.0)) return "lateral emissivity must be in [0, 1]";
    if (lateralActive && (!m.lateral.perimeter || !m.lateral.ambient))
        return "lateral exchange needs a perimeter and an ambient temperature";

    if (!o.steady) {
        if (!(o.endTime > 0.0)) return "end time must be positive";
        if (!(o.timeStep > 0.0)) return "time step must be positive";
    }
    if (!(o.theta >= 0.0 && o.theta <= 1.0)) return "theta must be in [0, 1]";
    if (!(o.tolerance > 0.0)) return "tolerance must be positive";
    if (o.maxIterations < 1) return "at least one Picard iteration is needed";
    if (o.recordEvery < 1) return "recordEvery must be at least 1";

    // A steady problem needs a fixed or convective boundary, otherwise the temperature level is undetermined.
    const bool anchored = m.left.kind != BoundaryKind::HeatFlux || m.right.kind != BoundaryKind::HeatFlux || lateralActive;
    if (o.steady && !anchored) return "a steady problem needs a temperature or a convection end, or a lateral exchange";
    return {};
}

Result solve(const Model& model, const Options& options) {
    Result result;
    result.error = validate(model, options);
    if (!result.error.empty()) return result;

    const std::size_t elements = static_cast<std::size_t>(model.elements);
    Discretization d;
    d.h = model.length / static_cast<double>(elements);
    d.x.resize(elements + 1);
    for (std::size_t i = 0; i <= elements; ++i) d.x[i] = static_cast<double>(i) * d.h;
    d.x.back() = model.length;
    const std::size_t n = d.x.size();
    result.nodes = d.x;

    std::vector<double> T(n, 0.0);
    if (model.initialTemperature) {
        for (std::size_t i = 0; i < n; ++i) T[i] = model.initialTemperature(d.x[i]);
    }

    try {
        if (options.steady) {
            bool ok = false;
            int iterations = 0;
            T = picardStep(model, d, options, T, 0.0, 0.0, nullptr, iterations, ok);
            result.maxPicardIterations = iterations;
            result.converged = ok;
            if (!ok) {
                result.error = "Picard iteration did not converge for the steady problem";
                return result;
            }
            result.times.push_back(0.0);
            result.temperatures.push_back(T);
            return result;
        }

        const Tridiagonal C = capacityMatrix(model, d, options.capacity);
        const int steps = std::max(1, static_cast<int>(std::ceil(options.endTime / options.timeStep - 1e-9)));
        const double dt = options.endTime / steps;

        result.times.push_back(0.0);
        result.temperatures.push_back(T);
        for (int s = 1; s <= steps; ++s) {
            const double tn = (s - 1) * dt;
            bool ok = false;
            int iterations = 0;
            T = picardStep(model, d, options, T, tn, dt, &C, iterations, ok);
            result.maxPicardIterations = std::max(result.maxPicardIterations, iterations);
            if (!ok) {
                std::ostringstream msg;
                msg << "Picard iteration did not converge at t = " << s * dt;
                result.error = msg.str();
                result.converged = false;
                return result;
            }
            if (s % options.recordEvery == 0 || s == steps) {
                result.times.push_back(s * dt);
                result.temperatures.push_back(T);
            }
        }
        result.converged = true;
    } catch (const std::runtime_error& e) {
        result.error = e.what();
        result.converged = false;
    }
    return result;
}

} // namespace OpenModel::Heat1D
