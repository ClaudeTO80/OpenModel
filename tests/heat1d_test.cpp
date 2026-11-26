/**
 * @file heat1d_test.cpp
 * @brief Verification of the one-dimensional heat conduction solver against analytical solutions, convergence orders,
 *        the discrete energy balance, and the input checks.
 *
 * The analytical references are: linear profiles (exact for linear elements), uniform source (exact at the nodes), a
 * variable cross-section and a non-linear conductivity (convergence with h), the insulated-tip fin, a Robin end, a flux
 * end, the radiation end (N = 1, reference by bisection), and the transient sine mode (Fourier series).
 */
#include <OpenModel/Heat1D.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

using namespace OpenModel::Heat1D;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

constexpr double kPi = 3.14159265358979323846;

/** @brief Constant time function. */
TimeFn constantTime(double v) {
    return [v](double) { return v; };
}

/** @brief Constant scalar function. */
ScalarFn constantScalar(double v) {
    return [v](double) { return v; };
}

/** @brief Model with unit properties, constant cross-section and no source; the caller sets the ends. */
Model baseModel(double length, int elements) {
    Model m;
    m.length = length;
    m.elements = elements;
    m.conductivity = [](double, double) { return 1.0; };
    m.volumetricHeatCapacity = constantScalar(1.0);
    m.area = constantScalar(1.0);
    return m;
}

Boundary temperatureEnd(double value) {
    Boundary b;
    b.kind = BoundaryKind::Temperature;
    b.value = constantTime(value);
    return b;
}

/** @brief Largest difference between the last recorded nodal temperatures and an exact profile. */
template <class Exact>
double maxNodalError(const Result& r, Exact exact) {
    double err = 0.0;
    const auto& T = r.temperatures.back();
    for (std::size_t i = 0; i < r.nodes.size(); ++i) err = std::max(err, std::fabs(T[i] - exact(r.nodes[i])));
    return err;
}

/** @brief Same as maxNodalError, but the exact profile depends on time too. */
template <class Exact>
double maxNodalErrorAt(const Result& r, double t, Exact exact) {
    double err = 0.0;
    const auto& T = r.temperatures.back();
    for (std::size_t i = 0; i < r.nodes.size(); ++i) err = std::max(err, std::fabs(T[i] - exact(r.nodes[i], t)));
    return err;
}

/** @brief Two-point Gauss points and weights mapped to the element [x1, x1 + h] (same rule as the solver). */
void gaussPoints(double x1, double h, double (&x)[2], double (&w)[2]) {
    const double a = 0.57735026918962576;
    x[0] = x1 + 0.5 * (1.0 - a) * h;
    x[1] = x1 + 0.5 * (1.0 + a) * h;
    w[0] = w[1] = 0.5 * h;
}

/** @brief Stored energy of a temperature profile: sum over the Gauss points of w * rho c * A * T_h. */
double storedEnergy(const Model& m, const Result& r, const std::vector<double>& T) {
    double E = 0.0;
    const double h = r.nodes[1] - r.nodes[0];
    for (std::size_t e = 0; e + 1 < r.nodes.size(); ++e) {
        double xg[2], wg[2];
        gaussPoints(r.nodes[e], h, xg, wg);
        for (int g = 0; g < 2; ++g) {
            const double s = (xg[g] - r.nodes[e]) / h;
            const double Tg = (1.0 - s) * T[e] + s * T[e + 1];
            E += wg[g] * m.volumetricHeatCapacity(xg[g]) * m.area(xg[g]) * Tg;
        }
    }
    return E;
}

/** @brief Total source power of the model at time t, sum over the Gauss points of w * A * q. */
double sourcePower(const Model& m, const Result& r, double t) {
    double S = 0.0;
    const double h = r.nodes[1] - r.nodes[0];
    for (std::size_t e = 0; e + 1 < r.nodes.size(); ++e) {
        double xg[2], wg[2];
        gaussPoints(r.nodes[e], h, xg, wg);
        for (int g = 0; g < 2; ++g) S += wg[g] * m.area(xg[g]) * m.source(xg[g], t);
    }
    return S;
}

/** @brief Test 1: steady conduction with two temperature ends has an exact linear profile. */
void testSteadyLinear() {
    Model m = baseModel(2.0, 4);
    m.conductivity = [](double, double) { return 50.0; };
    m.left = temperatureEnd(100.0);
    m.right = temperatureEnd(20.0);
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    CHECK(r.converged);
    const double err = maxNodalError(r, [](double x) { return 100.0 - 40.0 * x; });
    std::cout << "steady linear: max nodal error " << err << "\n";
    CHECK(err < 1e-9);
}

/** @brief Test 2: uniform source with zero ends; the linear-element solution is exact at the nodes. */
void testSteadySource() {
    Model m = baseModel(1.0, 8);
    m.source = [](double, double) { return 2.0; };
    m.left = temperatureEnd(0.0);
    m.right = temperatureEnd(0.0);
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    const double err = maxNodalError(r, [](double x) { return x * (1.0 - x); });
    std::cout << "steady source: max nodal error " << err << "\n";
    CHECK(err < 1e-10);
}

/** @brief Test 3: variable cross-section A(x) = 1 + x; the error must fall with the element size (second order). */
void testVariableArea() {
    auto run = [](int elements) {
        Model m = baseModel(1.0, elements);
        m.area = [](double x) { return 1.0 + x; };
        m.left = temperatureEnd(100.0);
        m.right = temperatureEnd(0.0);
        Options o;
        o.steady = true;
        const Result r = solve(m, o);
        CHECK(r.error.empty());
        return maxNodalError(r, [](double x) { return 100.0 - 100.0 * std::log(1.0 + x) / std::log(2.0); });
    };
    const double e10 = run(10);
    const double e40 = run(40);
    std::cout << "variable area: error N=10 " << e10 << ", N=40 " << e40 << "\n";
    CHECK(e40 < e10 / 10.0); // second order: a factor 16 expected
    CHECK(e40 < 1e-3);
}

/** @brief Test 4: fin with lateral convection and an insulated tip; exact cosh profile. */
void testFin() {
    Model m = baseModel(0.1, 100);
    m.conductivity = [](double, double) { return 200.0; };
    m.area = constantScalar(1e-4);
    m.lateral.perimeter = constantScalar(0.04);
    m.lateral.h = 50.0;
    m.lateral.ambient = constantTime(20.0);
    m.left = temperatureEnd(100.0);
    m.right = Boundary{}; // insulated tip
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    const double mm = std::sqrt(50.0 * 0.04 / (200.0 * 1e-4)); // = 10 1/m
    const double err = maxNodalError(r, [&](double x) {
        return 20.0 + 80.0 * std::cosh(mm * (0.1 - x)) / std::cosh(mm * 0.1);
    });
    std::cout << "fin: max nodal error " << err << " K\n";
    CHECK(err < 1e-3);
}

/** @brief Test 5: Robin end (convection) with a Dirichlet end; the solution is linear and exact. */
void testConvectionEnd() {
    Model m = baseModel(1.0, 5);
    m.conductivity = [](double, double) { return 10.0; };
    m.area = constantScalar(2.0);
    m.left = temperatureEnd(100.0);
    m.right.kind = BoundaryKind::Convection;
    m.right.h = 25.0;
    m.right.ambient = constantTime(20.0);
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    const double kA = 10.0 * 2.0;
    const double hA = 25.0 * 2.0;
    const double TL = (kA * 100.0 / 1.0 + hA * 20.0) / (kA / 1.0 + hA);
    const double err = maxNodalError(r, [&](double x) { return 100.0 + (TL - 100.0) * x; });
    std::cout << "convection end: max nodal error " << err << "\n";
    CHECK(err < 1e-8);
}

/** @brief Test 6: prescribed heat flux at one end; the linear profile has slope Q / (kA). */
void testHeatFluxEnd() {
    Model m = baseModel(1.0, 4);
    m.conductivity = [](double, double) { return 5.0; };
    m.left = temperatureEnd(10.0);
    m.right.kind = BoundaryKind::HeatFlux;
    m.right.value = constantTime(100.0); // W/m^2, area 1 -> 100 W
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    const double err = maxNodalError(r, [](double x) { return 10.0 + 20.0 * x; });
    std::cout << "heat flux end: max nodal error " << err << "\n";
    CHECK(err < 1e-9);
}

/** @brief Test 7: radiation at the right end with one element (exact at the node); reference by bisection. */
void testRadiationEnd() {
    Model m = baseModel(0.5, 1);
    m.conductivity = [](double, double) { return 1.0; };
    m.left = temperatureEnd(600.0);
    m.right.kind = BoundaryKind::Convection;
    m.right.h = 0.0;
    m.right.emissivity = 0.8;
    m.right.ambient = constantTime(300.0);
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    CHECK(r.converged);

    const double sigma = m.stefanBoltzmann;
    auto balance = [&](double TL) { return 1.0 * (600.0 - TL) / 0.5 - 0.8 * sigma * (TL * TL * TL * TL - 300.0 * 300.0 * 300.0 * 300.0); };
    double lo = 300.0;
    double hi = 600.0;
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (balance(mid) > 0.0) lo = mid; else hi = mid;
    }
    const double TLref = 0.5 * (lo + hi);
    const double TL = r.temperatures.back()[1];
    std::cout << "radiation end: TL " << TL << " K, bisection " << TLref << " K, Picard iterations "
              << r.maxPicardIterations << "\n";
    CHECK(std::fabs(TL - TLref) < 1e-6);
    CHECK(r.maxPicardIterations > 1); // the non-linearity is really iterated
}

/**
 * @brief Test 8: non-linear conductivity k = 1 + 0.01 T, two fixed ends; exact through the Kirchhoff transform.
 *
 * The Kirchhoff potential Phi = T + 0.005 T^2 is linear in x. With k linear in T and the Gauss rule of the element, the
 * element flux is k_bar (T_{e+1} - T_e) = Phi_{e+1} - Phi_e exactly, so the linear-element solution coincides with the
 * exact one at the nodes: the check is an absolute one (round-off and Picard tolerance), not a convergence rate.
 */
void testNonLinearConductivity() {
    Model m = baseModel(1.0, 20);
    m.conductivity = [](double, double T) { return 1.0 + 0.01 * T; };
    m.left = temperatureEnd(300.0);
    m.right = temperatureEnd(400.0);
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    CHECK(r.converged);
    const double phi0 = 300.0 + 0.005 * 300.0 * 300.0;
    const double phiL = 400.0 + 0.005 * 400.0 * 400.0;
    const double err = maxNodalError(r, [&](double x) {
        const double phi = phi0 + (phiL - phi0) * x;
        return (-1.0 + std::sqrt(1.0 + 0.02 * phi)) / 0.01;
    });
    std::cout << "non-linear k: max nodal error " << err << " K (Picard iterations " << r.maxPicardIterations << ")\n";
    CHECK(err < 1e-8);
}

/** @brief Test 9: transient sine mode with zero ends: T = sin(pi x) exp(-pi^2 t) for k = rho c = 1, A = 1. */
void testTransientSineMode() {
    auto run = [](double theta, Capacity capacity) {
        Model m = baseModel(1.0, 40);
        m.left = temperatureEnd(0.0);
        m.right = temperatureEnd(0.0);
        m.initialTemperature = [](double x) { return std::sin(kPi * x); };
        Options o;
        o.theta = theta;
        o.timeStep = 1e-3;
        o.endTime = 0.1;
        o.capacity = capacity;
        const Result r = solve(m, o);
        CHECK(r.error.empty());
        CHECK(r.converged);
        return maxNodalErrorAt(r, o.endTime, [](double x, double t) {
            return std::sin(kPi * x) * std::exp(-kPi * kPi * t);
        });
    };
    const double eCN = run(0.5, Capacity::Consistent);
    const double eIE = run(1.0, Capacity::Consistent);
    const double eCNLumped = run(0.5, Capacity::Lumped);
    std::cout << "transient sine: Crank-Nicolson " << eCN << ", implicit Euler " << eIE << ", CN lumped " << eCNLumped << "\n";
    CHECK(eCN < 1e-3);
    CHECK(eIE < 5e-3);
    CHECK(eIE > eCN);            // first order in time is less accurate
    CHECK(eCNLumped < 5e-3);
}

/** @brief Test 10: discrete energy balance with variable area, time-dependent source and insulated ends. */
void testEnergyBalance() {
    Model m = baseModel(1.0, 10);
    m.conductivity = [](double, double) { return 2.0; };
    m.volumetricHeatCapacity = constantScalar(3.0);
    m.area = [](double x) { return 1.0 + 0.5 * x; };
    m.source = [](double, double t) { return 5.0 * (1.0 + std::cos(t)); };
    m.initialTemperature = [](double x) { return 20.0 + 10.0 * x; };
    Options o;
    o.theta = 0.5;
    o.timeStep = 0.01;
    o.endTime = 1.0;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    CHECK(r.temperatures.size() == r.times.size());

    // E(t_{n+1}) - E(t_n) = dt [theta S(t_{n+1}) + (1 - theta) S(t_n)] holds exactly for the theta method.
    const double dt = r.times[1] - r.times[0];
    const std::size_t steps = r.times.size() - 1;
    double predicted = 0.0;
    for (std::size_t n = 0; n < steps; ++n) {
        predicted += dt * (o.theta * sourcePower(m, r, r.times[n + 1]) + (1.0 - o.theta) * sourcePower(m, r, r.times[n]));
    }
    const double E0 = storedEnergy(m, r, r.temperatures.front());
    const double E1 = storedEnergy(m, r, r.temperatures.back());
    const double residual = std::fabs((E1 - E0) - predicted);
    std::cout << "energy balance: stored change " << (E1 - E0) << ", predicted " << predicted << ", residual " << residual << "\n";
    CHECK(residual < 1e-7 * (std::fabs(E0) + std::fabs(predicted)));
}

/** @brief Test 11: recordEvery keeps every n-th step and the last one. */
void testRecording() {
    Model m = baseModel(1.0, 4);
    m.left = temperatureEnd(0.0);
    m.right = temperatureEnd(0.0);
    m.initialTemperature = [](double x) { return x; };
    Options o;
    o.timeStep = 0.01;
    o.endTime = 0.1;
    o.recordEvery = 10;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    CHECK(r.times.size() == 2);
    CHECK(std::fabs(r.times.back() - 0.1) < 1e-12);
}

/** @brief Test 12: input checks and the errors reported in Result. */
void testValidation() {
    Options transient;
    transient.timeStep = 0.1;
    transient.endTime = 1.0;

    Model noLength = baseModel(1.0, 4);
    noLength.length = 0.0;
    CHECK(!validate(noLength, transient).empty());

    Model noArea = baseModel(1.0, 4);
    noArea.area = nullptr;
    CHECK(!validate(noArea, transient).empty());

    Model noValue = baseModel(1.0, 4);
    noValue.left.kind = BoundaryKind::Temperature; // no value
    CHECK(!validate(noValue, transient).empty());

    Options badTheta = transient;
    badTheta.theta = 1.5;
    CHECK(!validate(baseModel(1.0, 4), badTheta).empty());

    Model lateralNoAmbient = baseModel(1.0, 4);
    lateralNoAmbient.lateral.perimeter = constantScalar(1.0);
    lateralNoAmbient.lateral.h = 3.0;
    CHECK(!validate(lateralNoAmbient, transient).empty());

    // A steady problem with both ends insulated and no lateral exchange has no unique temperature level.
    Options steady;
    steady.steady = true;
    Model insulated = baseModel(1.0, 4);
    const std::string singular = validate(insulated, steady);
    CHECK(!singular.empty());
    const Result refused = solve(insulated, steady);
    CHECK(!refused.error.empty());
    CHECK(!refused.converged);

    // A valid input has no error.
    CHECK(validate(baseModel(1.0, 4), transient).empty());
}

/** @brief Test 13: a Picard iteration that is cut short is reported, not silently accepted. */
void testPicardFailureIsReported() {
    Model m = baseModel(0.5, 1);
    m.left = temperatureEnd(600.0);
    m.right.kind = BoundaryKind::Convection;
    m.right.emissivity = 0.8;
    m.right.ambient = constantTime(300.0);
    Options o;
    o.steady = true;
    o.maxIterations = 1;
    const Result r = solve(m, o);
    CHECK(!r.error.empty());
    CHECK(!r.converged);
}

} // namespace

int main() {
    testSteadyLinear();
    testSteadySource();
    testVariableArea();
    testFin();
    testConvectionEnd();
    testHeatFluxEnd();
    testRadiationEnd();
    testNonLinearConductivity();
    testTransientSineMode();
    testEnergyBalance();
    testRecording();
    testValidation();
    testPicardFailureIsReported();

    if (g_failures != 0) {
        std::cerr << g_failures << " heat1d check(s) failed\n";
        return 1;
    }
    std::cout << "heat1d_test: all checks passed\n";
    return 0;
}
