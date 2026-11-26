/**
 * @file heat1d_script_test.cpp
 * @brief Checks of the one-dimensional heat script: section geometry (A and P), the commands, the error messages with
 *        their line numbers, a steady run against an exact profile, and the CSV output.
 */
#include <OpenModel/Heat1DScript.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

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

/** @brief Parses a script that must be valid; reports the errors otherwise. */
ScriptProblem parseOk(const std::string& text) {
    ScriptProblem p;
    std::vector<std::string> errs;
    const bool ok = parseScript(text, p, errs);
    if (!ok || !errs.empty()) {
        std::cerr << "unexpected errors:\n";
        for (const auto& e : errs) std::cerr << "  " << e << "\n";
        ++g_failures;
    }
    return p;
}

/** @brief True when the script is refused and one of the messages contains `fragment`. */
bool failsWith(const std::string& text, const std::string& fragment) {
    ScriptProblem p;
    std::vector<std::string> errs;
    if (parseScript(text, p, errs)) return false;
    for (const auto& e : errs) {
        if (e.find(fragment) != std::string::npos) return true;
    }
    std::cerr << "expected '" << fragment << "', got:\n";
    for (const auto& e : errs) std::cerr << "  " << e << "\n";
    return false;
}

/** @brief Analysis, section and thermal lines only: the caller adds the conditions and the step. */
std::string prefix(const std::string& section) {
    return "analysis heat 1d length=1 elements=4\n" + section + "\nthermal k=1 rhocp=1\n";
}

/** @brief Valid steady script around the section line given by the caller. */
std::string steadyWithSection(const std::string& section) {
    return "analysis heat 1d length=1 elements=4\n" + section +
           "\nthermal k=1 rhocp=1\ntemperature end=left T=0\ntemperature end=right T=1\nstep steady\n";
}

/** @brief Test 1: section geometry gives the area A and the perimeter P used for the lateral exchange. */
void testSectionGeometry() {
    const ScriptProblem rect = parseOk(steadyWithSection("section rect a=0.1 b=0.2"));
    CHECK(std::fabs(rect.model.area(0.3) - 0.02) < 1e-12);
    CHECK(std::fabs(rect.model.lateral.perimeter(0.3) - 0.6) < 1e-12);

    const ScriptProblem circle = parseOk(steadyWithSection("section circle r=0.01"));
    CHECK(std::fabs(circle.model.area(0.3) - kPi * 1e-4) < 1e-15);
    CHECK(std::fabs(circle.model.lateral.perimeter(0.3) - 2.0 * kPi * 0.01) < 1e-15);

    // Tube: the area is the annulus; both surfaces are exposed, so the perimeter is the sum of the two.
    const ScriptProblem tube = parseOk(steadyWithSection("section tube ro=0.02 ri=0.01"));
    CHECK(std::fabs(tube.model.area(0.3) - kPi * 3e-4) < 1e-15);
    CHECK(std::fabs(tube.model.lateral.perimeter(0.3) - 2.0 * kPi * 0.03) < 1e-15);

    // Regular hexagon with circumradius 1 has side 1: area 3 sqrt(3)/2, perimeter 6.
    const ScriptProblem hex = parseOk(steadyWithSection("section polygon sides=6 r=1"));
    CHECK(std::fabs(hex.model.area(0.3) - 3.0 * std::sqrt(3.0) / 2.0) < 1e-12);
    CHECK(std::fabs(hex.model.lateral.perimeter(0.3) - 6.0) < 1e-12);

    // Square with circumradius 1/sqrt(2) has side 1: area 1, perimeter 4.
    const ScriptProblem square = parseOk(steadyWithSection("section polygon sides=4 r=0.70710678118654752"));
    CHECK(std::fabs(square.model.area(0.3) - 1.0) < 1e-12);
    CHECK(std::fabs(square.model.lateral.perimeter(0.3) - 4.0) < 1e-12);

    // Table: piecewise linear in x, constant outside the ends (which are 0 and the length).
    const ScriptProblem table = parseOk(steadyWithSection("section table x=0,0.5,1 A=1,2,3 P=4,4,4"));
    CHECK(std::fabs(table.model.area(0.25) - 1.5) < 1e-12);
    CHECK(std::fabs(table.model.area(0.75) - 2.5) < 1e-12);
    CHECK(std::fabs(table.model.lateral.perimeter(0.9) - 4.0) < 1e-12);
}

/** @brief Test 2: steady bar with a flux end has the exact linear profile T = T0 + (q A / (k A)) x. */
void testSteadyFluxBar() {
    const std::string text =
        "analysis heat 1d length=1 elements=5\n"
        "section rect a=0.1 b=0.1\n"           // A = 0.01
        "thermal k=10 rhocp=1\n"
        "temperature end=left T=100\n"
        "flux end=right q=50\n"                // Q = 50 * 0.01 = 0.5 W
        "step steady\n";
    const ScriptProblem p = parseOk(text);
    const Result r = runScript(p);
    CHECK(r.error.empty());
    CHECK(r.converged);
    double err = 0.0;
    for (std::size_t i = 0; i < r.nodes.size(); ++i) {
        const double exact = 100.0 + 5.0 * r.nodes[i]; // slope Q / (k A) = 0.5 / 0.1 = 5
        err = std::max(err, std::fabs(r.temperatures.back()[i] - exact));
    }
    std::cout << "steady flux bar: max nodal error " << err << "\n";
    CHECK(err < 1e-9);
}

/** @brief Test 3: uniform source and zero ends in a square bar: T = q x (L - x) / (2 k), exact at the nodes. */
void testSteadySource() {
    const std::string text =
        "analysis heat 1d length=1 elements=4\n"
        "section rect a=1 b=1\n"
        "thermal k=1 rhocp=1\n"
        "heat q=2\n"
        "temperature end=left T=0\n"
        "temperature end=right T=0\n"
        "step steady\n";
    const Result r = runScript(parseOk(text));
    CHECK(r.error.empty());
    const double mid = r.temperatures.back()[2]; // node at x = 0.5
    std::cout << "steady source: T(0.5) = " << mid << " (exact 0.25)\n";
    CHECK(std::fabs(mid - 0.25) < 1e-10);
}

/**
 * @brief Test 4: transient tube with both ends at 0 and an initial temperature 1, against the Fourier series
 *        T(x,t) = sum over odd n of 4/(n pi) sin(n pi x) exp(-n^2 pi^2 alpha t), with alpha = k / (rho c) = 0.1.
 *        Lumped capacity, Crank-Nicolson, 20 elements and dt = 0.01: the difference is the discretisation error.
 */
void testTransientRuns() {
    const std::string text =
        "analysis heat 1d length=1 elements=20\n"
        "section tube ro=0.02 ri=0.01\n"
        "thermal k=50 rhocp=500\n"
        "initial T=1\n"
        "temperature end=left T=0\n"
        "temperature end=right T=0\n"
        "step transient dt=0.01 end=0.5 theta=0.5 capacity=lumped\n";
    const Result r = runScript(parseOk(text));
    CHECK(r.error.empty());
    CHECK(r.converged);
    CHECK(r.times.size() == 51);

    const double alpha = 0.1;
    const double t = 0.5;
    const double x = 0.5;
    double exact = 0.0;
    for (int n = 1; n < 200; n += 2) {
        const double an = n * kPi;
        exact += 4.0 / an * std::sin(an * x) * std::exp(-an * an * alpha * t);
    }
    const double mid = r.temperatures.back()[10];
    std::cout << "transient tube: T(0.5, 0.5) = " << mid << ", series " << exact << "\n";
    CHECK(std::fabs(mid - exact) < 5e-3);
}

/** @brief Test 5: the errors carry the line number and the problem. */
void testErrors() {
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nfoo bar\n", "line 2"));
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nfoo bar\n", "unknown command"));

    // Missing sections and lines.
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nthermal k=1 rhocp=1\nstep steady\n", "missing a section"));
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nsection rect a=1 b=1\nstep steady\n", "missing the material data"));
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nsection rect a=1 b=1\nthermal k=1 rhocp=1\n", "missing a step"));

    // Order and repetition.
    CHECK(failsWith("section rect a=1 b=1\nanalysis heat 1d length=1 elements=4\n", "must come first"));
    CHECK(failsWith(prefix("section rect a=1 b=1") + "step steady\nstep steady\n", "only one step"));
    CHECK(failsWith(prefix("section rect a=1 b=1") + "section circle r=1\n", "only one section"));
    CHECK(failsWith(prefix("section rect a=1 b=1") + "temperature end=left T=0\ntemperature end=left T=5\n",
                    "already has a condition"));

    // Values and keys.
    CHECK(failsWith("analysis heat 1d length=1 elements=2.5\n", "elements must be an integer"));
    CHECK(failsWith(steadyWithSection("section rect a=1 b=1 c=2"), "unknown key 'c'"));
    CHECK(failsWith(steadyWithSection("section rect a=abc b=1"), "a is not a number"));
    CHECK(failsWith(steadyWithSection("section rect a=1 b=-1"), "a and b must be positive"));
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nthermal k= rhocp=1\n", "malformed word"));
    CHECK(failsWith(steadyWithSection("section polygon sides=2 r=1"), "sides must be an integer >= 3"));
    CHECK(failsWith(steadyWithSection("section tube ro=0.01 ri=0.02"), "need 0 <= ri < ro"));

    // Table: strictly increasing x, and it must cover the length.
    CHECK(failsWith(steadyWithSection("section table x=0,0.5,0.4 A=1,1,1 P=1,1,1"), "strictly increasing"));
    CHECK(failsWith(steadyWithSection("section table x=0,0.5 A=1,1 P=1,1"), "table x must go from 0 to the length"));

    // Boundaries and solver.
    CHECK(failsWith(steadyWithSection("section rect a=1 b=1") + "flux end=middle q=1\n", "end must be left or right"));
    CHECK(failsWith(prefix("section rect a=1 b=1") + "convection end=right h=5\nstep steady\n", "missing Tinf="));
    CHECK(failsWith(prefix("section rect a=1 b=1") + "job J solver=calculix\nstep steady\n", "solver=internal"));
    CHECK(failsWith(prefix("section rect a=1 b=1") + "step transient dt=0 end=1\n", "dt must be positive"));
    CHECK(failsWith(prefix("section rect a=1 b=1") + "step transient dt=0.1 end=1 capacity=diagonal\n", "consistent or lumped"));

    // A steady problem needs a fixed or convective end; with two insulated ends it has no unique level.
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nsection rect a=1 b=1\nthermal k=1 rhocp=1\nstep steady\n",
                    "steady problem needs"));
}

/** @brief Test 6: the CSV has one row per recorded time and node, after the header. */
void testCsv() {
    const std::string text =
        "analysis heat 1d length=1 elements=2\n"
        "section rect a=1 b=1\n"
        "thermal k=1 rhocp=1\n"
        "temperature end=left T=0\n"
        "temperature end=right T=1\n"
        "step steady\n";
    const Result r = runScript(parseOk(text));
    const std::string csv = resultToCsv(r);
    std::istringstream lines(csv);
    std::string line;
    std::size_t count = 0;
    std::getline(lines, line);
    CHECK(line == "time,x,temperature");
    while (std::getline(lines, line)) ++count;
    CHECK(count == r.times.size() * r.nodes.size());
}

/**
 * @brief Test 7: two materials in series from the script, with the interface on a node. The solution is the exact
 *        piecewise-linear profile of the composite wall (k = 1 on 0..0.4, k = 4 on 0.4..1, ends at 100 and 0).
 */
void testTwoMaterials() {
    const std::string text =
        "analysis heat 1d\n"
        "material id=A k=1 rhocp=1\n"
        "material id=B k=4 rhocp=1\n"
        "segment material=A length=0.4 elements=4\n"
        "segment material=B length=0.6 elements=6\n"
        "section rect a=1 b=1\n"
        "temperature end=left T=100\n"
        "temperature end=right T=0\n"
        "step steady\n";
    const ScriptProblem p = parseOk(text);
    const Result r = runScript(p);
    CHECK(r.error.empty());
    CHECK(r.converged);
    CHECK(r.nodes.size() == 11); // 4 + 6 elements, 11 nodes
    const double q = 100.0 / (0.4 / 1.0 + 0.6 / 4.0);
    const double Ti = 100.0 - 0.4 * q;
    double err = 0.0;
    for (std::size_t i = 0; i < r.nodes.size(); ++i) {
        const double x = r.nodes[i];
        const double exact = x <= 0.4 ? 100.0 - q * x : Ti - q * (x - 0.4) / 4.0;
        err = std::max(err, std::fabs(r.temperatures.back()[i] - exact));
    }
    std::cout << "two materials: max nodal error " << err << " K, interface " << Ti << " K\n";
    CHECK(err < 1e-9);

    // A transient run with the same two layers runs and stays between the end temperatures.
    const std::string transient =
        "analysis heat 1d\nmaterial id=A k=1 rhocp=2\nmaterial id=B k=4 rhocp=3\n"
        "segment material=A length=0.5 elements=10\nsegment material=B length=0.5 elements=10\n"
        "section rect a=1 b=1\ninitial T=50\ntemperature end=left T=0\ntemperature end=right T=0\n"
        "step transient dt=0.01 end=0.2 theta=0.5 capacity=lumped\n";
    const Result rt = runScript(parseOk(transient));
    CHECK(rt.error.empty());
    CHECK(rt.converged);
    for (double T : rt.temperatures.back()) CHECK(T >= -1e-9 && T <= 50.0 + 1e-9);
}

/** @brief Test 8: errors in the material and segment lines, each with its line number. */
void testMaterialErrors() {
    const std::string head = "analysis heat 1d\nsection rect a=1 b=1\n";
    CHECK(failsWith(head + "material id=A k=1 rhocp=1\nmaterial id=A k=2 rhocp=1\n", "material 'A' is defined twice"));
    CHECK(failsWith(head + "material id=A k=1 rhocp=1\nsegment material=B length=1 elements=2\n", "unknown material 'B'"));
    CHECK(failsWith(head + "segment material=A length=1 elements=2\nmaterial id=A k=1 rhocp=1\n", "unknown material 'A'"));
    CHECK(failsWith(head + "material id=A rhocp=1\n", "missing k="));
    CHECK(failsWith(head + "material id=A k=-1 rhocp=1\n", "k must be positive"));
    CHECK(failsWith(head + "material k=1 rhocp=1\n", "missing id="));
    CHECK(failsWith(head + "material id=A k=1 rhocp=1\nsegment material=A elements=2\n", "missing length="));
    CHECK(failsWith(head + "material id=A k=1 rhocp=1\nsegment material=A length=1\n", "missing elements="));
    CHECK(failsWith(head + "material id=A k=1 rhocp=1\nsegment material=A length=0 elements=2\n", "length must be positive"));
    CHECK(failsWith(head + "material id=A k=1 rhocp=1\nthermal k=1 rhocp=1\nsegment material=A length=1 elements=2\n",
                    "cannot be combined"));
    CHECK(failsWith("analysis heat 1d length=1 elements=4\nsection rect a=1 b=1\nmaterial id=A k=1 rhocp=1\n"
                    "segment material=A length=1 elements=4\nstep steady\n",
                    "follow from the segment lines"));
    CHECK(failsWith("analysis heat 1d\nsection rect a=1 b=1\nmaterial id=A k=1 rhocp=1\nstep steady\n",
                    "missing the material data"));
    // A table for A(x) must cover the total length of the segments (here 0.5 + 0.5 = 1).
    CHECK(failsWith("analysis heat 1d\nmaterial id=A k=1 rhocp=1\nsegment material=A length=0.5 elements=2\n"
                    "segment material=A length=0.5 elements=2\nsection table x=0,0.5,0.9 A=1,1,1 P=1,1,1\n"
                    "temperature end=left T=0\ntemperature end=right T=1\nstep steady\n",
                    "table x must go from 0 to the length"));
}

/**
 * @brief Test 9: conductivity given as a table kT. The table of k = 1 + 0.01 T (linear) reproduces the Kirchhoff exact
 *        solution; the table is interpolated and extended by its end values; a table with a kink converges monotonically.
 */
void testTabulatedConductivity() {
    const std::string linear =
        "analysis heat 1d\nmaterial id=M kT=300:4,350:4.5,400:5 rhocp=1\nsegment material=M length=1 elements=20\n"
        "section rect a=1 b=1\ntemperature end=left T=300\ntemperature end=right T=400\nstep steady\n";
    const ScriptProblem p = parseOk(linear);
    CHECK(std::fabs(p.model.conductivity(0.5, 325.0) - 4.25) < 1e-12); // interpolated
    CHECK(std::fabs(p.model.conductivity(0.5, 250.0) - 4.0) < 1e-12);  // extended below
    CHECK(std::fabs(p.model.conductivity(0.5, 500.0) - 5.0) < 1e-12);  // extended above
    const Result r = runScript(p);
    CHECK(r.error.empty());
    CHECK(r.converged);
    // Kirchhoff potential Phi = T + 0.005 T^2 is linear in x, as in heat1d_test.
    const double phi0 = 300.0 + 0.005 * 300.0 * 300.0;
    const double phiL = 400.0 + 0.005 * 400.0 * 400.0;
    double err = 0.0;
    for (std::size_t i = 0; i < r.nodes.size(); ++i) {
        const double phi = phi0 + (phiL - phi0) * r.nodes[i];
        const double exact = (-1.0 + std::sqrt(1.0 + 0.02 * phi)) / 0.01;
        err = std::max(err, std::fabs(r.temperatures.back()[i] - exact));
    }
    std::cout << "tabulated linear k: max nodal error " << err << " K (Picard " << r.maxPicardIterations << ")\n";
    CHECK(err < 1e-8);

    // Non-linear table with a kink; the profile must rise monotonically from 300 to 900 with positive flux.
    const std::string kink =
        "analysis heat 1d\nmaterial id=M kT=300:10,600:1,900:2 rhocp=1\nsegment material=M length=1 elements=40\n"
        "section rect a=1 b=1\ntemperature end=left T=300\ntemperature end=right T=900\nstep steady\n";
    const Result rk = runScript(parseOk(kink));
    CHECK(rk.error.empty());
    CHECK(rk.converged);
    const auto& T = rk.temperatures.back();
    bool monotone = true;
    for (std::size_t i = 1; i < T.size(); ++i) monotone = monotone && T[i] > T[i - 1];
    CHECK(monotone);
    CHECK(std::fabs(T.front() - 300.0) < 1e-9 && std::fabs(T.back() - 900.0) < 1e-9);
    std::cout << "tabulated kink k: Picard iterations " << rk.maxPicardIterations << "\n";

    // A tabulated material next to a constant one.
    const std::string mixed =
        "analysis heat 1d\nmaterial id=A kT=300:2,500:3 rhocp=1\nmaterial id=B k=4 rhocp=1\n"
        "segment material=A length=0.5 elements=10\nsegment material=B length=0.5 elements=10\n"
        "section rect a=1 b=1\ntemperature end=left T=300\ntemperature end=right T=500\nstep steady\n";
    const Result rm = runScript(parseOk(mixed));
    CHECK(rm.error.empty());
    CHECK(rm.converged);
}

/** @brief Test 10: the errors of a conductivity table, and k= together with kT=. */
void testConductivityErrors() {
    const std::string head = "analysis heat 1d\nsection rect a=1 b=1\n";
    CHECK(failsWith(head + "material id=M k=1 kT=300:1,400:2 rhocp=1\n", "give k= or kT=, not both"));
    CHECK(failsWith(head + "material id=M rhocp=1\n", "missing k= or kT="));
    CHECK(failsWith(head + "material id=M kT=300:4 rhocp=1\n", "needs at least two pairs"));
    CHECK(failsWith(head + "material id=M kT=400:4,300:5 rhocp=1\n", "strictly increasing"));
    CHECK(failsWith(head + "material id=M kT=300:0,400:5 rhocp=1\n", "must be positive"));
    CHECK(failsWith(head + "material id=M kT=300-4,400:5 rhocp=1\n", "must be pairs T:k"));
    CHECK(failsWith(head + "material id=M kT=300:4, rhocp=1\n", "must be pairs T:k"));
}

} // namespace

int main() {
    testSectionGeometry();
    testTwoMaterials();
    testMaterialErrors();
    testTabulatedConductivity();
    testConductivityErrors();
    testSteadyFluxBar();
    testSteadySource();
    testTransientRuns();
    testErrors();
    testCsv();

    if (g_failures != 0) {
        std::cerr << g_failures << " heat1d script check(s) failed\n";
        return 1;
    }
    std::cout << "heat1d_script_test: all checks passed\n";
    return 0;
}
