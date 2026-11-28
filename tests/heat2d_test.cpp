/**
 * @file heat2d_test.cpp
 * @brief Verification of the two-dimensional thin-plate heat solver: exact linear fields, a double Fourier reference,
 *        the transient sine mode, holes, symmetry, thickness scaling, the non-linear conductivity, and the input checks.
 */
#include <OpenModel/Heat2D.hpp>

#include <cmath>
#include <iostream>
#include <string>

using namespace OpenModel::Heat2D;

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

OpenModel::Heat1D::TimeFn constantTime(double v) {
    return [v](double) { return v; };
}

Boundary temperatureSide(double value) {
    Boundary b;
    b.kind = BoundaryKind::Temperature;
    b.value = constantTime(value);
    return b;
}

/** @brief Insulated side: flux kind with no value. */
Boundary insulatedSide() {
    return Boundary{};
}

/** @brief Plate of unit properties; the caller sets the sides, source and holes. */
Model unitPlate(int nx, int ny) {
    Model m;
    m.lx = 1.0;
    m.ly = 1.0;
    m.nx = nx;
    m.ny = ny;
    m.conductivity = [](double, double, double) { return 1.0; };
    m.volumetricHeatCapacity = [](double, double) { return 1.0; };
    return m;
}

Options steadyOptions() {
    Options o;
    o.steady = true;
    return o;
}

Options transientOptions(double dt, double endTime, double theta) {
    Options o;
    o.steady = false;
    o.timeStep = dt;
    o.endTime = endTime;
    o.theta = theta;
    return o;
}

/** @brief Node id of (i, j) for a plate with nx elements along x. */
int nodeId(const Model& m, int i, int j) {
    return j * (m.nx + 1) + i;
}

/** @brief Reference centre temperature of -lap(u) = 1, u = 0 on the unit square (double Fourier series). */
double squarePoissonCentre() {
    double sum = 0.0;
    for (int m = 1; m < 200; m += 2) {
        for (int n = 1; n < 200; n += 2) {
            const double sgn = (((m - 1) / 2 + (n - 1) / 2) % 2 == 0) ? 1.0 : -1.0;
            sum += sgn * 16.0 / (kPi * kPi * kPi * kPi * m * n * (static_cast<double>(m) * m + static_cast<double>(n) * n));
        }
    }
    return sum;
}

void testLinearField() {
    // T = 100 x on the plate: exact for bilinear elements, insulated top and bottom.
    Model m = unitPlate(8, 4);
    m.left = temperatureSide(0.0);
    m.right = temperatureSide(100.0);
    m.bottom = insulatedSide();
    m.top = insulatedSide();
    const Result r = solve(m, steadyOptions());
    CHECK(r.error.empty());
    CHECK(r.converged);
    double maxErr = 0.0;
    for (int j = 0; j <= m.ny; ++j) {
        for (int i = 0; i <= m.nx; ++i) {
            const double x = static_cast<double>(i) / m.nx;
            maxErr = std::max(maxErr, std::abs(r.temperatures[0][nodeId(m, i, j)] - 100.0 * x));
        }
    }
    CHECK(maxErr < 1e-9);
}

void testConvectionEnd() {
    // Left at 100, right convective (h = 10, T_inf = 20), insulated sides: exact linear profile.
    Model m = unitPlate(10, 3);
    m.left = temperatureSide(100.0);
    m.right.kind = BoundaryKind::Convection;
    m.right.h = 10.0;
    m.right.ambient = constantTime(20.0);
    m.bottom = insulatedSide();
    m.top = insulatedSide();
    const Result r = solve(m, steadyOptions());
    CHECK(r.error.empty());
    const double TL = (100.0 + 10.0 * 20.0) / (1.0 + 10.0);
    double maxErr = 0.0;
    for (int j = 0; j <= m.ny; ++j) {
        for (int i = 0; i <= m.nx; ++i) {
            const double x = static_cast<double>(i) / m.nx;
            const double exact = 100.0 + (TL - 100.0) * x;
            maxErr = std::max(maxErr, std::abs(r.temperatures[0][nodeId(m, i, j)] - exact));
        }
    }
    CHECK(maxErr < 1e-9);
}

void testSourceAgainstFourier() {
    // -lap(T) = 1 on the unit square with T = 0 on the four sides.
    Model m = unitPlate(40, 40);
    m.source = [](double, double, double) { return 1.0; };
    m.left = temperatureSide(0.0);
    m.right = temperatureSide(0.0);
    m.bottom = temperatureSide(0.0);
    m.top = temperatureSide(0.0);
    const Result r = solve(m, steadyOptions());
    CHECK(r.error.empty());
    const double centre = r.temperatures[0][nodeId(m, 20, 20)];
    const double ref = squarePoissonCentre();
    CHECK(std::abs(centre - ref) < 1e-3);
    std::cout << "source centre: " << centre << "  Fourier: " << ref << "\n";
}

void testTransientSineMode() {
    // T(x, 0) = sin(pi x), zero at the x-ends, insulated y-sides: T = sin(pi x) exp(-pi^2 t).
    Model m = unitPlate(40, 4);
    m.initialTemperature = [](double x, double) { return std::sin(kPi * x); };
    m.left = temperatureSide(0.0);
    m.right = temperatureSide(0.0);
    m.bottom = insulatedSide();
    m.top = insulatedSide();
    m.volumetricHeatCapacity = [](double, double) { return 1.0; };
    const Result r = solve(m, transientOptions(1e-3, 0.1, 0.5));
    CHECK(r.error.empty());
    CHECK(r.converged);
    CHECK(!r.temperatures.empty());
    const double exact = std::sin(kPi * 0.5) * std::exp(-kPi * kPi * 0.1);
    const double numeric = r.temperatures.back()[nodeId(m, 20, 2)];
    std::cout << "sine mode numeric " << numeric << "  exact " << exact << "\n";
    CHECK(std::abs(numeric - exact) < 1e-3);
}

void testLumpedCapacityClose() {
    Model m = unitPlate(40, 4);
    m.initialTemperature = [](double x, double) { return std::sin(kPi * x); };
    m.left = temperatureSide(0.0);
    m.right = temperatureSide(0.0);
    m.bottom = insulatedSide();
    m.top = insulatedSide();
    Options o = transientOptions(1e-3, 0.1, 0.5);
    o.capacity = OpenModel::Heat1D::Capacity::Lumped;
    const Result r = solve(m, o);
    CHECK(r.error.empty());
    const double exact = std::sin(kPi * 0.5) * std::exp(-kPi * kPi * 0.1);
    CHECK(std::abs(r.temperatures.back()[nodeId(m, 20, 2)] - exact) < 2e-3);
}

void testCircularHoleIsolatedSymmetry() {
    // Centred insulated hole, outer sides at zero, uniform source: symmetric about x = 0.5.
    Model m = unitPlate(40, 40);
    Hole h;
    h.shape = HoleShape::Circle;
    h.cx = 0.5;
    h.cy = 0.5;
    h.r = 0.2;
    m.holes.push_back(h);
    m.source = [](double, double, double) { return 1.0; };
    m.left = temperatureSide(0.0);
    m.right = temperatureSide(0.0);
    m.bottom = temperatureSide(0.0);
    m.top = temperatureSide(0.0);
    const Result r = solve(m, steadyOptions());
    CHECK(r.error.empty());
    CHECK(r.converged);
    CHECK(!r.active[static_cast<std::size_t>(nodeId(m, 20, 20))]);
    double maxAsym = 0.0;
    for (int j = 0; j <= m.ny; ++j) {
        for (int i = 0; i <= m.nx; ++i) {
            const std::size_t a = static_cast<std::size_t>(nodeId(m, i, j));
            const std::size_t b = static_cast<std::size_t>(nodeId(m, m.nx - i, j));
            if (!r.active[a]) continue;
            maxAsym = std::max(maxAsym, std::abs(r.temperatures[0][a] - r.temperatures[0][b]));
        }
    }
    CHECK(maxAsym < 1e-10);
    // The hole is heated only by the solid around it: the solid temperature must be positive.
    CHECK(r.temperatures[0][static_cast<std::size_t>(nodeId(m, 20, 5))] > 0.0);
}

void testHoleAtFixedTemperature() {
    // Insulated plate sides, no source, hole at 50: the whole solid settles to 50.
    Model m = unitPlate(20, 20);
    Hole h;
    h.shape = HoleShape::Rectangle;
    h.x0 = 0.3;
    h.x1 = 0.6;
    h.y0 = 0.4;
    h.y1 = 0.7;
    m.holes.push_back(h);
    m.hole = temperatureSide(50.0);
    m.left = insulatedSide();
    m.right = insulatedSide();
    m.bottom = insulatedSide();
    m.top = insulatedSide();
    const Result r = solve(m, steadyOptions());
    CHECK(r.error.empty());
    double maxErr = 0.0;
    for (std::size_t i = 0; i < r.active.size(); ++i) {
        if (!r.active[i]) continue;
        maxErr = std::max(maxErr, std::abs(r.temperatures[0][i] - 50.0));
    }
    CHECK(maxErr < 1e-9);
}

void testThicknessScaling() {
    // Every term is proportional to the thickness: the steady solution does not depend on it.
    Model a = unitPlate(12, 12);
    a.source = [](double x, double, double) { return 1.0 + x; };
    a.left = temperatureSide(0.0);
    a.right = temperatureSide(10.0);
    a.bottom = insulatedSide();
    a.top = insulatedSide();
    Model b = a;
    b.thickness = 3.0;
    const Result ra = solve(a, steadyOptions());
    const Result rb = solve(b, steadyOptions());
    CHECK(ra.error.empty() && rb.error.empty());
    double maxDiff = 0.0;
    for (std::size_t i = 0; i < ra.temperatures[0].size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(ra.temperatures[0][i] - rb.temperatures[0][i]));
    }
    CHECK(maxDiff < 1e-10);
}

void testNonlinearConductivity() {
    // k = 1 + 0.01 T with T = 0 at x = 0 and T = 100 at x = 1 (Kirchhoff transform: u = T + 0.005 T^2 is linear in x).
    // The exact profile solves 0.005 T^2 + T = 150 x. The discrete solution is compared to it with a finite-element
    // error tolerance.
    Model m = unitPlate(40, 2);
    m.conductivity = [](double, double, double T) { return 1.0 + 0.01 * T; };
    m.left = temperatureSide(0.0);
    m.right = temperatureSide(100.0);
    m.bottom = insulatedSide();
    m.top = insulatedSide();
    const Result r = solve(m, steadyOptions());
    CHECK(r.error.empty());
    CHECK(r.converged);
    double maxErr = 0.0;
    for (int i = 0; i <= m.nx; ++i) {
        const double x = static_cast<double>(i) / m.nx;
        const double exact = (-1.0 + std::sqrt(1.0 + 3.0 * x)) / 0.01;
        maxErr = std::max(maxErr, std::abs(r.temperatures[0][nodeId(m, i, 1)] - exact));
    }
    std::cout << "nonlinear conductivity max error " << maxErr << " K, Picard " << r.maxPicardIterations << "\n";
    CHECK(maxErr < 0.05);
    CHECK(r.maxPicardIterations > 1);
}

void testValidation() {
    Model plain = unitPlate(4, 4);
    plain.left = insulatedSide();
    plain.right = insulatedSide();
    plain.bottom = insulatedSide();
    plain.top = insulatedSide();

    const std::string noAnchor = validate(plain, steadyOptions());
    CHECK(noAnchor.find("steady") != std::string::npos);

    Model covered = unitPlate(4, 4);
    Hole big;
    big.shape = HoleShape::Rectangle;
    big.x0 = 0.0;
    big.y0 = 0.0;
    big.x1 = 1.0;
    big.y1 = 1.0;
    covered.holes.push_back(big);
    covered.left = temperatureSide(0.0);
    CHECK(validate(covered, steadyOptions()).find("no solid") != std::string::npos);

    Model outside = unitPlate(4, 4);
    Hole far;
    far.shape = HoleShape::Circle;
    far.cx = 0.9;
    far.cy = 0.5;
    far.r = 0.2;
    outside.holes.push_back(far);
    outside.left = temperatureSide(0.0);
    CHECK(validate(outside, steadyOptions()).find("inside the plate") != std::string::npos);

    Model corner = unitPlate(4, 4);
    corner.left = temperatureSide(0.0);
    corner.bottom = temperatureSide(10.0);
    CHECK(validate(corner, steadyOptions()).find("different temperatures") != std::string::npos);

    Model badK = unitPlate(4, 4);
    badK.conductivity = [](double, double, double) { return -1.0; };
    badK.left = temperatureSide(0.0);
    const Result rb = solve(badK, steadyOptions());
    CHECK(!rb.error.empty());

    Options noEnd = transientOptions(0.1, 0.0, 0.5);
    CHECK(!validate(unitPlate(4, 4), noEnd).empty());
}

} // namespace

void testThinPlateAssessment() {
    // Input checks.
    CHECK(!assessThinPlate(0.0, 1.0, 0.0).error.empty());
    CHECK(!assessThinPlate(0.1, 0.0, 0.0).error.empty());
    CHECK(!assessThinPlate(0.1, 1.0, -1.0).error.empty());
    CHECK(!assessThinPlate(0.1, 1.0, 0.0, 0.0).error.empty());

    // Adiabatic faces normal to z: Bi = 0, applicable, with the exactness note.
    const ThinPlateAssessment adiabatic = assessThinPlate(0.1, 1.0, 0.0);
    CHECK(adiabatic.error.empty());
    CHECK(adiabatic.applicable);
    CHECK(adiabatic.biot == 0.0);
    CHECK(adiabatic.notes.size() == 2);

    // Bi = h (t/2) / k = 2 * 0.05 / 1 = 0.1: exactly at the limit, applicable.
    // The default limit is 0.02: the same Bi = 0.1 is then not applicable.
    CHECK(assessThinPlate(0.1, 1.0, 2.0).biotLimit == 0.02);
    CHECK(!assessThinPlate(0.1, 1.0, 2.0).applicable);
    const ThinPlateAssessment atLimit = assessThinPlate(0.1, 1.0, 2.0, 0.1);
    CHECK(atLimit.applicable);
    CHECK(std::abs(atLimit.biot - 0.1) < 1e-15);

    // Bi = 2.5 * 0.05 = 0.125 > 0.1: not applicable.
    const ThinPlateAssessment above = assessThinPlate(0.1, 1.0, 2.5, 0.1);
    CHECK(!above.applicable);
    CHECK(std::abs(above.biot - 0.125) < 1e-15);

    // The Biot number grows with the thickness and falls with the conductivity.
    CHECK(assessThinPlate(0.2, 1.0, 1.0).biot > assessThinPlate(0.1, 1.0, 1.0).biot);
    CHECK(assessThinPlate(0.1, 10.0, 1.0).biot < assessThinPlate(0.1, 1.0, 1.0).biot);
}

int main() {
    testLinearField();
    testConvectionEnd();
    testSourceAgainstFourier();
    testTransientSineMode();
    testLumpedCapacityClose();
    testCircularHoleIsolatedSymmetry();
    testHoleAtFixedTemperature();
    testThicknessScaling();
    testNonlinearConductivity();
    testValidation();
    testThinPlateAssessment();

    if (g_failures == 0) {
        std::cout << "heat2d_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_test: " << g_failures << " check(s) failed\n";
    return 1;
}
