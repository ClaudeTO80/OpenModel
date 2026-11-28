/**
 * @file heat2d_gmsh_test.cpp
 * @brief Steady conduction on a Gmsh triangle mesh of an annulus, against the exact radial solution.
 *
 * Outer circle R = 1 at 0, inner circle a = 0.2 at 50, uniform source q = 1, k = 1, thickness 1. The exact solution is
 * u(r) = -r^2/4 + A ln r + B with A = (Th + (a^2 - R^2)/4) / ln(a/R) and B = R^2/4 - A ln R. The boundary is curved, so
 * the mesh is conforming: the error must fall as the element size falls, not stall as it does for a staircase boundary.
 */
#include <OpenModel/Heat2DGmsh.hpp>
#include <OpenModel/Heat2DMesh.hpp>

#include <algorithm>
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

constexpr double R = 1.0;
constexpr double A_IN = 0.2;
constexpr double TH = 50.0;

/** @brief Exact radial solution at radius r. */
double exactAt(double r) {
    const double A = (TH + (A_IN * A_IN - R * R) / 4.0) / std::log(A_IN / R);
    const double B = R * R / 4.0 - A * std::log(R);
    return -r * r / 4.0 + A * std::log(r) + B;
}

/** @brief Solves on the annulus of element size h and returns the largest nodal error, or a negative value on failure. */
double maxError(double h, std::size_t& nodes) {
    std::string err;
    const TriangleMesh mesh = meshAnnulusGmsh(R, A_IN, h, err);
    if (!err.empty() || mesh.nodes.empty()) {
        std::cerr << "mesh failed: " << err << "\n";
        return -1.0;
    }
    MeshModel model;
    model.conductivity = [](double, double, double) { return 1.0; };
    model.source = [](double, double, double) { return 1.0; };
    OpenModel::Heat1D::Boundary outer;
    outer.kind = BoundaryKind::Temperature;
    outer.value = [](double) { return 0.0; };
    OpenModel::Heat1D::Boundary inner;
    inner.kind = BoundaryKind::Temperature;
    inner.value = [](double) { return TH; };
    model.boundaries[1] = outer;
    model.boundaries[2] = inner;
    Options o;
    o.steady = true;
    const MeshResult r = solveMesh(mesh, model, o);
    if (!r.error.empty() || !r.converged) {
        std::cerr << "solve failed: " << r.error << "\n";
        return -1.0;
    }
    nodes = mesh.nodes.size();
    double worst = 0.0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const double x = mesh.nodes[i][0];
        const double y = mesh.nodes[i][1];
        const double rr = std::hypot(x, y);
        if (rr < A_IN - 1e-12 || rr > R + 1e-12) continue;
        worst = std::max(worst, std::abs(r.temperatures[i] - exactAt(rr)));
    }
    return worst;
}

void testConvergenceOnCurvedBoundary() {
    // Four sizes: the error must fall at each step, and the observed order between the two finest must be near 2.
    const double sizes[4] = {0.2, 0.1, 0.05, 0.025};
    double errors[4] = {0.0, 0.0, 0.0, 0.0};
    for (int k = 0; k < 4; ++k) {
        std::size_t nodes = 0;
        errors[k] = maxError(sizes[k], nodes);
        std::cout << "h = " << sizes[k] << ": " << nodes << " nodes, max error " << errors[k] << " K\n";
        CHECK(errors[k] > 0.0);
    }
    for (int k = 1; k < 4; ++k) CHECK(errors[k] < errors[k - 1]);
    if (errors[2] > 0.0 && errors[3] > 0.0) {
        const double order = std::log(errors[2] / errors[3]) / std::log(2.0);
        std::cout << "observed order between h = 0.05 and 0.025: " << order << "\n";
        // Linear triangles on a smooth field with a conforming boundary: second order, measured here near 2.
        CHECK(order > 1.5);
    }
    CHECK(errors[3] < 0.02);
}

} // namespace

int main() {
    testConvergenceOnCurvedBoundary();
    if (g_failures == 0) {
        std::cout << "heat2d_gmsh_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_gmsh_test: " << g_failures << " check(s) failed\n";
    return 1;
}
