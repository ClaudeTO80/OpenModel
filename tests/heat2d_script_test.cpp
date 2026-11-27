/**
 * @file heat2d_script_test.cpp
 * @brief Verification of the two-dimensional heat script: a solution written as a script against the double Fourier
 *        reference, holes and hole conditions, a uniform transient, the error messages with their line numbers, and the
 *        CSV output.
 */
#include <OpenModel/Heat2DScript.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

using namespace OpenModel::Heat2D;

namespace {
constexpr double kPi = 3.14159265358979323846;
}

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

/** @brief Parses and runs a script; returns the errors (empty on success) and the result. */
bool runText(const std::string& text, Result& result, std::vector<std::string>& errors, ScriptProblem* keep = nullptr) {
    ScriptProblem problem;
    if (!parseScript(text, problem, errors)) return false;
    result = runScript(problem);
    if (keep) *keep = problem;
    return true;
}

bool hasError(const std::vector<std::string>& errors, const std::string& needle) {
    for (const auto& e : errors)
        if (e.find(needle) != std::string::npos) return true;
    return false;
}

void testFourierBySquareScript() {
    // -lap(T) = 1 on the unit square, zero on the four sides: the same reference as heat2d_test.
    const std::string text =
        "analysis heat 2d\n"
        "plate lx=1 ly=1 nx=40 ny=40\n"
        "material k=1 rhocp=1\n"
        "heat q=1\n"
        "temperature side=left T=0\n"
        "temperature side=right T=0\n"
        "temperature side=bottom T=0\n"
        "temperature side=top T=0\n"
        "step steady\n";
    Result r;
    std::vector<std::string> errors;
    ScriptProblem problem;
    CHECK(parseScript(text, problem, errors));
    CHECK(errors.empty());
    r = runScript(problem);
    CHECK(r.error.empty());
    CHECK(r.converged);
    double sum = 0.0;
    for (int m = 1; m < 200; m += 2) {
        for (int n = 1; n < 200; n += 2) {
            const double sgn = (((m - 1) / 2 + (n - 1) / 2) % 2 == 0) ? 1.0 : -1.0;
            sum += sgn * 16.0 / (kPi * kPi * kPi * kPi * m * n * (static_cast<double>(m) * m + static_cast<double>(n) * n));
        }
    }
    const double centre = r.temperatures[0][static_cast<std::size_t>(20 * r.nodesX + 20)];
    std::cout << "script centre " << centre << "  Fourier " << sum << "\n";
    CHECK(std::abs(centre - sum) < 1e-3);
}

void testHoleScript() {
    const std::string text =
        "analysis heat 2d\n"
        "plate lx=1 ly=1 nx=40 ny=40\n"
        "material k=1 rhocp=1\n"
        "hole circle cx=0.5 cy=0.5 r=0.2\n"
        "heat q=1\n"
        "temperature side=left T=0\n"
        "temperature side=right T=0\n"
        "temperature side=bottom T=0\n"
        "temperature side=top T=0\n"
        "step steady\n";
    Result r;
    std::vector<std::string> errors;
    CHECK(runText(text, r, errors));
    CHECK(errors.empty());
    CHECK(r.error.empty());
    CHECK(!r.active[static_cast<std::size_t>(20 * r.nodesX + 20)]);
    CHECK(r.active[static_cast<std::size_t>(5 * r.nodesX + 20)]);
}

void testHoleConditionScript() {
    // Insulated plate, rectangular hole at 50: the solid settles at 50 (the hole is the only temperature side).
    const std::string text =
        "analysis heat 2d\n"
        "plate lx=1 ly=1 nx=20 ny=20\n"
        "material k=1 rhocp=1\n"
        "hole rect x0=0.3 y0=0.4 x1=0.6 y1=0.7\n"
        "hole temperature T=50\n"
        "step steady\n";
    Result r;
    std::vector<std::string> errors;
    CHECK(runText(text, r, errors));
    CHECK(errors.empty());
    double maxErr = 0.0;
    for (std::size_t i = 0; i < r.active.size(); ++i) {
        if (r.active[i]) maxErr = std::max(maxErr, std::abs(r.temperatures[0][i] - 50.0));
    }
    CHECK(maxErr < 1e-9);
}

void testTransientUniform() {
    // Insulated everywhere, initial 100, no source: the temperature stays 100.
    const std::string text =
        "analysis heat 2d\n"
        "plate lx=0.2 ly=0.1 nx=10 ny=5 thickness=0.005\n"
        "material k=50 rhocp=3.9e6\n"
        "initial T=100\n"
        "step transient dt=0.01 end=0.05 theta=0.5\n";
    Result r;
    std::vector<std::string> errors;
    CHECK(runText(text, r, errors));
    CHECK(errors.empty());
    CHECK(r.error.empty());
    CHECK(r.times.size() == 6);
    double maxErr = 0.0;
    for (const auto& row : r.temperatures) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (r.active[i]) maxErr = std::max(maxErr, std::abs(row[i] - 100.0));
        }
    }
    CHECK(maxErr < 1e-8);
}

void testCsv() {
    const std::string text =
        "analysis heat 2d\n"
        "plate lx=1 ly=1 nx=2 ny=2\n"
        "material k=1 rhocp=1\n"
        "hole rect x0=0.4 y0=0.4 x1=0.6 y1=0.6\n"
        "initial T=10\n"
        "temperature side=left T=10\n"
        "step steady\n";
    ScriptProblem problem;
    std::vector<std::string> errors;
    CHECK(parseScript(text, problem, errors));
    const Result r = runScript(problem);
    const std::string csv = resultToCsv(r, problem);
    CHECK(csv.rfind("time,x,y,temperature\n", 0) == 0);
    // 3 x 3 nodes; the hole covers no node of the 2 x 2 grid (the element centroids are at 0.25 and 0.75), so all 9 nodes are
    // active: 9 data rows.
    std::size_t rows = 0;
    for (char ch : csv)
        if (ch == '\n') ++rows;
    CHECK(rows == 1 + 9);
}

void testErrors() {
    std::vector<std::string> errors;
    ScriptProblem p;

    // Unknown command, with its line number.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2 ny=2\nfoo bar\n", p, errors));
    CHECK(hasError(errors, "line 3: unknown command 'foo'"));

    // Missing key.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2\n", p, errors));
    CHECK(hasError(errors, "missing ny="));

    // Not a number.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=abc ly=1 nx=2 ny=2\n", p, errors));
    CHECK(hasError(errors, "lx= is not a number"));

    // Non-integer element count.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2.5 ny=2\n", p, errors));
    CHECK(hasError(errors, "whole numbers"));

    // Side given twice.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2 ny=2\nmaterial k=1 rhocp=1\n"
                       "temperature side=left T=0\ntemperature side=left T=5\nstep steady\n",
                       p, errors));
    CHECK(hasError(errors, "line 5: side 'left' already has a condition"));

    // Bad hole shape.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2 ny=2\nhole triangle cx=1\n", p, errors));
    CHECK(hasError(errors, "hole shape must be circle or rect"));

    // Non-solver job.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2 ny=2\nmaterial k=1 rhocp=1\n"
                       "job A solver=calculix\nstep steady\n",
                       p, errors));
    CHECK(hasError(errors, "only the internal solver"));

    // Missing sections.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\n", p, errors));
    CHECK(hasError(errors, "missing 'plate' line"));
    CHECK(hasError(errors, "missing 'step' line"));

    // Steady without any temperature or convection: refused by the model check, without a line number.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2 ny=2\nmaterial k=1 rhocp=1\nstep steady\n", p, errors));
    CHECK(hasError(errors, "steady problem needs"));

    // Stray word after a command.
    errors.clear();
    CHECK(!parseScript("analysis heat 2d\nplate lx=1 ly=1 nx=2 ny=2 extra\n", p, errors));
    CHECK(hasError(errors, "unexpected word 'extra'"));
}

} // namespace

int main() {
    testFourierBySquareScript();
    testHoleScript();
    testHoleConditionScript();
    testTransientUniform();
    testCsv();
    testErrors();

    if (g_failures == 0) {
        std::cout << "heat2d_script_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_script_test: " << g_failures << " check(s) failed\n";
    return 1;
}
