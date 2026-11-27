/**
 * @file heat1d_display_test.cpp
 * @brief Tests of the display mesh of a 1D heat problem: the axis tube radius, the element values along the axis, and
 *        the errors. The values are checked against the position of each element, so the test does not depend on the
 *        order in which the blocks are written.
 */
#include <OpenModel/Heat1DDisplay.hpp>
#include <OpenModel/Heat1DScript.hpp>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace OpenModel::Heat1D;

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << __FILE__ << ":" << __LINE__ << ": check failed: " #cond "\n";    \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

const char* kChain =
    "analysis heat 1d\n"
    "material id=STEEL k=50 rhocp=3.9e6\n"
    "material id=ALU k=200 rhocp=2.4e6\n"
    "segment material=STEEL length=0.5 elements=20 shape=rect a=0.04 b=0.04\n"
    "segment material=ALU length=0.3 elements=6 shape=tube ro=0.05 ri=0.02\n"
    "segment material=STEEL length=0.2 elements=4 shape=circle r=0.03\n"
    "temperature end=left T=100\n"
    "convection end=right h=10 Tinf=20\n"
    "step steady\n";

ScriptProblem parseOk(const std::string& text) {
    ScriptProblem p;
    std::vector<std::string> errors;
    const bool ok = parseScript(text, p, errors);
    if (!ok) {
        for (const auto& e : errors) std::cerr << "script error: " << e << "\n";
        ++g_failures;
    }
    return p;
}

/** @brief Mean x of the corners of each triangle of a block, in the order of the block. */
std::vector<double> centroidsOf(const MeshData& mesh, const ElementBlock& block) {
    std::vector<double> xs;
    const std::size_t k = block.nodesPerElement;
    for (std::size_t e = 0; e < block.ids.size(); ++e) {
        double sum = 0.0;
        for (std::size_t j = 0; j < k; ++j) sum += mesh.nodeCoordinates(block.connectivity[e * k + j])[0];
        xs.push_back(sum / static_cast<double>(k));
    }
    return xs;
}

/** @brief Test 1: the axis is a tube of radius 2 % of the extent of the model (1 m long here: radius 0.02). */
void testAxisRadius() {
    const ScriptProblem p = parseOk(kChain);
    const DisplayResult d = displayMesh(p);
    CHECK(d.error.empty());
    const ElementBlock& axis = d.mesh.getElementBlock("heat:AXIS");
    CHECK(!axis.empty());
    double worst = 0.0;
    for (const auto index : axis.connectivity) {
        const auto c = d.mesh.nodeCoordinates(index);
        worst = std::max(worst, std::fabs(std::hypot(c[1], c[2]) - 0.02));
    }
    std::cout << "axis tube: radius error " << worst << " m (radius 0.02 m)\n";
    CHECK(worst < 1e-12);
    // A different percentage gives a different radius.
    DisplayOptions o;
    o.axisRadiusPercent = 5.0;
    const DisplayResult d5 = displayMesh(p, o);
    double r5 = 0.0;
    for (const auto index : d5.mesh.getElementBlock("heat:AXIS").connectivity) {
        const auto c = d5.mesh.nodeCoordinates(index);
        r5 = std::max(r5, std::hypot(c[1], c[2]));
    }
    CHECK(std::fabs(r5 - 0.05) < 1e-12);
}

/** @brief Test 2: one value per element, the temperature at each element's position along the axis. */
void testValuesFollowPosition() {
    const ScriptProblem p = parseOk(kChain);
    // T = x at the nodes: the 1D solution at an element is then its position, whatever the segment.
    std::vector<double> T(p.model.nodes.size());
    for (std::size_t i = 0; i < T.size(); ++i) T[i] = p.model.nodes[i];
    const DisplayResult d = displayMesh(p, {}, &T);
    CHECK(d.error.empty());
    CHECK(d.values.size() == d.mesh.getNumElements());
    std::size_t at = 0;
    double worst = 0.0;
    for (const auto& block : d.mesh.getElementBlocks()) {
        const std::vector<double> xs = centroidsOf(d.mesh, block);
        for (double x : xs) {
            worst = std::max(worst, std::fabs(d.values[at] - x));
            ++at;
        }
    }
    std::cout << "element values: max error " << worst << " (" << d.values.size() << " elements)\n";
    CHECK(at == d.values.size());
    CHECK(worst < 1e-12);

    // A constant temperature gives the same value everywhere.
    std::vector<double> flat(p.model.nodes.size(), 42.0);
    const DisplayResult f = displayMesh(p, {}, &flat);
    bool constant = !f.values.empty();
    for (double v : f.values) constant = constant && std::fabs(v - 42.0) < 1e-12;
    CHECK(constant);

    // Without temperatures there are no values.
    CHECK(displayMesh(p).values.empty());
}

/** @brief Test 3: a tabulated section, or temperatures that do not match the nodes, are reported. */
void testErrors() {
    const ScriptProblem table = parseOk("analysis heat 1d\nmaterial id=A k=1 rhocp=1\n"
                                        "segment material=A length=1 elements=4\n"
                                        "section table x=0,1 A=1,1 P=1,1\n"
                                        "temperature end=left T=0\ntemperature end=right T=0\nstep steady\n");
    CHECK(!displayMesh(table).error.empty());

    const ScriptProblem p = parseOk(kChain);
    std::vector<double> wrong(3, 0.0);
    CHECK(displayMesh(p, {}, &wrong).error.find("do not match") != std::string::npos);

    DisplayOptions bad;
    bad.ringPoints = 2;
    CHECK(!displayMesh(p, bad).error.empty());
    bad.ringPoints = 32;
    bad.axisRadiusPercent = 0.0;
    CHECK(!displayMesh(p, bad).error.empty());
}

} // namespace

int main() {
    testAxisRadius();
    testValuesFollowPosition();
    testErrors();
    if (g_failures != 0) {
        std::cerr << g_failures << " heat1d display check(s) failed\n";
        return 1;
    }
    std::cout << "heat1d_display_test: all checks passed\n";
    return 0;
}
