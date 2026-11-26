#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/**
 * @file Heat1D.hpp
 * @brief One-dimensional heat conduction on a segment [0, L]: linear finite elements, theta time integration and Picard
 *        iteration for the non-linear terms (temperature-dependent conductivity, radiation).
 *
 * The complete model solved is
 * \f[
 *   \rho c_p A(x)\,\frac{\partial T}{\partial t}
 *   = \frac{\partial}{\partial x}\Big(k(x,T)\,A(x)\,\frac{\partial T}{\partial x}\Big)
 *   + A(x)\,q(x,t)
 *   - P(x)\,\big[h_\ell\,(T-T_\infty) + \varepsilon_\ell\,\sigma\,(T^4-T_\infty^4)\big]
 * \f]
 * with the two ends given by a temperature, a heat flux, or a convection-radiation exchange with the ambient
 * temperature \f$T_\infty(t)\f$. In the steady case the time derivative is dropped.
 *
 * Units are the caller's own. Radiation needs absolute temperatures (kelvin) and the Stefan-Boltzmann constant of the
 * same unit system. The radiation sink is the ambient temperature of the same exchange (there is no separate sink).
 *
 * Method: uniform mesh of N linear elements; 2-point Gauss quadrature for every integral (so A(x), k(x,T), q(x,t) and
 * P(x) may vary inside an element); time integration with the theta method (theta = 0 explicit Euler, 0.5 Crank-Nicolson,
 * 1 implicit Euler); the non-linear coefficients are frozen at the current iterate and the step is repeated until the
 * temperature stops changing (Picard). The linear systems are tridiagonal and are solved by the Thomas algorithm.
 *
 * The derivation, the choice of the options and the list of the simplifications are in docs/HEAT_1D.md.
 */

namespace OpenModel::Heat1D {

using ScalarFn = std::function<double(double)>;
using TimeFn = std::function<double(double)>;

/** @brief Kind of condition at one end of the segment. */
enum class BoundaryKind {
    Temperature, ///< Dirichlet: the end temperature is `value(t)`. Required `value`.
    HeatFlux,    ///< Neumann: heat per unit area entering the domain is `value(t)`. An empty `value` is an insulated end.
    Convection   ///< Robin: heat entering = (h + radiation coefficient) * (T_ambient - T), see Boundary::h.
};

/** @brief Condition at one end. */
struct Boundary {
    BoundaryKind kind = BoundaryKind::HeatFlux;
    TimeFn value;                  ///< Temperature: T(t). HeatFlux: q''(t) in W/m^2 (per unit area, times the area at the end).
    double h = 0.0;                ///< Convection: convection coefficient h >= 0.
    double emissivity = 0.0;       ///< Convection: radiation exchange with the ambient, 0 <= emissivity <= 1.
    TimeFn ambient;                ///< Convection: ambient temperature T_inf(t) (also the radiation sink).
};

/** @brief Lateral exchange along the segment, per unit length P(x) (the perimeter of the section). */
struct Lateral {
    ScalarFn perimeter;            ///< P(x). Empty = no lateral exchange.
    double h = 0.0;                ///< Convection coefficient h_l >= 0.
    double emissivity = 0.0;       ///< Radiation emissivity, 0 <= emissivity <= 1.
    TimeFn ambient;                ///< Ambient temperature T_inf(t) (also the radiation sink). Required when active.
};

/** @brief The model: geometry, properties, sources and conditions. */
struct Model {
    double length = 1.0;           ///< L > 0.
    int elements = 10;             ///< Number of uniform linear elements N >= 1 (nodes = N + 1).

    /// Thermal conductivity k(x, T) > 0. Required. Use a function of x only for a constant-property model.
    std::function<double(double x, double T)> conductivity;
    /// Volumetric heat capacity rho * c_p(x) > 0. Required for a transient run (unused in a steady run).
    ScalarFn volumetricHeatCapacity;
    /// Cross-section A(x) > 0. Required.
    ScalarFn area;
    /// Volumetric heat source q(x, t). Empty = no source.
    std::function<double(double x, double t)> source;

    Lateral lateral;               ///< Lateral exchange (empty perimeter = adiabatic sides).

    Boundary left;                 ///< Condition at x = 0.
    Boundary right;                ///< Condition at x = L.

    /// Initial temperature T(x) at t = 0. Empty = 0 everywhere.
    ScalarFn initialTemperature;

    double stefanBoltzmann = 5.670374419e-8; ///< sigma, in the unit system of the model.
};

/** @brief How the capacity matrix is built. */
enum class Capacity {
    Consistent, ///< Consistent mass matrix (same shape functions as the stiffness). Default.
    Lumped      ///< Row-sum lumping: diagonal matrix, no coupling between nodes in the time derivative.
};

/** @brief Numerical options. */
struct Options {
    bool steady = false;           ///< Steady problem (no time derivative; the source is taken at t = 0).
    double theta = 1.0;            ///< Time-integration parameter in [0, 1].
    double timeStep = 0.0;         ///< Requested step (transient); the step is shortened to end exactly at endTime.
    double endTime = 0.0;          ///< End of the run (transient), > 0.
    Capacity capacity = Capacity::Consistent;
    double tolerance = 1e-10;      ///< Picard stop: max change <= tolerance * max(1, max |T|).
    int maxIterations = 100;       ///< Picard iterations allowed per step.
    int recordEvery = 1;           ///< Store the solution every this many steps (the last step is always stored).
};

/** @brief Result of a run. */
struct Result {
    std::string error;                       ///< Empty on success; otherwise the first problem found.
    bool converged = false;                  ///< True when every step reached the Picard tolerance.
    int maxPicardIterations = 0;             ///< Largest number of Picard iterations used by one step.
    std::vector<double> nodes;               ///< Node positions x_0 .. x_N.
    std::vector<double> times;               ///< Recorded times (a single 0 for a steady run).
    std::vector<std::vector<double>> temperatures; ///< temperatures[k][i] = T at node i and time times[k].
};

/**
 * @brief Checks a model and its options.
 * @return an empty string when the input is usable, otherwise the first problem found.
 */
std::string validate(const Model& model, const Options& options);

/**
 * @brief Solves the model.
 *
 * Invalid input is reported in Result::error (no exception). A failure of the Picard iteration or a singular system is
 * reported the same way, with Result::converged false.
 */
Result solve(const Model& model, const Options& options);

} // namespace OpenModel::Heat1D
