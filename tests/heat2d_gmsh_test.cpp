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

#include <gmsh.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

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
        worst = std::max(worst, std::abs(r.temperatures[0][i] - exactAt(rr)));
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

/** @brief Writes a planar model to a STEP file: a rectangle or annulus, built with the OpenCASCADE kernel. */
void writeStep(const std::string& path, bool annulus) {
    gmsh::initialize();
    gmsh::option::setNumber("General.Terminal", 0);
    gmsh::model::add("writer");
    if (annulus) {
        const int outer = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, R, R);
        const int inner = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, A_IN, A_IN);
        std::vector<std::pair<int, int>> cut;
        std::vector<std::vector<std::pair<int, int>>> cutMap;
        gmsh::model::occ::cut({{2, outer}}, {{2, inner}}, cut, cutMap);
    } else {
        const int rect = gmsh::model::occ::addRectangle(0.0, 0.0, 0.0, 2.0, 1.0);
        const int hole = gmsh::model::occ::addDisk(1.0, 0.5, 0.0, 0.3, 0.3);
        std::vector<std::pair<int, int>> cut;
        std::vector<std::vector<std::pair<int, int>>> cutMap;
        gmsh::model::occ::cut({{2, rect}}, {{2, hole}}, cut, cutMap);
    }
    gmsh::model::occ::synchronize();
    gmsh::write(path);
    gmsh::finalize();
}

/** @brief Annulus model of the test: outer tag 1 at 0, hole tag 2 at TH, source 1. */
MeshModel annulusModel() {
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
    return model;
}

/** @brief Largest nodal error of a result on the annulus against the exact solution (independent of node order). */
double annulusError(const TriangleMesh& mesh, const MeshResult& r) {
    double worst = 0.0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const double rr = std::hypot(mesh.nodes[i][0], mesh.nodes[i][1]);
        if (rr < A_IN - 1e-12 || rr > R + 1e-12) continue;
        worst = std::max(worst, std::abs(r.temperatures[0][i] - exactAt(rr)));
    }
    return worst;
}

void testStepRoundTrip() {
    const std::string path = "heat2d_annulus_roundtrip.step";
    writeStep(path, true);
    std::string e1, e2;
    const TriangleMesh direct = meshAnnulusGmsh(R, A_IN, 0.05, e1);
    const TriangleMesh imported = meshPlanarStepGmsh(path, 0.05, e2);
    CHECK(e1.empty());
    CHECK(e2.empty());
    CHECK(!imported.nodes.empty());
    CHECK(imported.nodes.size() == direct.nodes.size());
    CHECK(imported.triangles.size() == direct.triangles.size());
    Options o;
    o.steady = true;
    const MeshResult r1 = solveMesh(direct, annulusModel(), o);
    const MeshResult r2 = solveMesh(imported, annulusModel(), o);
    CHECK(r1.error.empty() && r2.error.empty());
    const double err1 = annulusError(direct, r1);
    const double err2 = annulusError(imported, r2);
    std::cout << "STEP round trip: " << imported.nodes.size() << " nodes, max error direct " << err1
              << " K, imported " << err2 << " K\n";
    CHECK(std::abs(err1 - err2) < 1e-9);
}

void testPlateWithHoleFromStep() {
    const std::string path = "heat2d_plate_hole.step";
    writeStep(path, false);
    std::string err;
    const TriangleMesh mesh = meshPlanarStepGmsh(path, 0.08, err);
    CHECK(err.empty());
    CHECK(!mesh.nodes.empty() && !mesh.triangles.empty());

    bool hasOuter = false, hasHole = false;
    for (const auto& e : mesh.edges) {
        hasOuter = hasOuter || e.tag == 1;
        hasHole = hasHole || e.tag == 2;
    }
    CHECK(hasOuter && hasHole);

    MeshModel model = annulusModel();
    Options o;
    o.steady = true;
    const MeshResult r = solveMesh(mesh, model, o);
    CHECK(r.error.empty());
    CHECK(r.converged);

    // Boundary values: outer contour at 0, hole at TH.
    double outerWorst = 0.0, holeWorst = 0.0, maxT = -1e300;
    for (const auto& e : mesh.edges) {
        const double t = r.temperatures[0][static_cast<std::size_t>(e.a)];
        if (e.tag == 1) outerWorst = std::max(outerWorst, std::abs(t));
        if (e.tag == 2) holeWorst = std::max(holeWorst, std::abs(t - TH));
    }
    for (double t : r.temperatures[0]) maxT = std::max(maxT, t);
    std::cout << "plate with hole from STEP: " << mesh.nodes.size() << " nodes, outer max |T| " << outerWorst
              << " K, hole max |T-50| " << holeWorst << " K, max T " << maxT << " K\n";
    CHECK(outerWorst < 1e-9);
    CHECK(holeWorst < 1e-9);
    // Minimum principle: -lap T = q > 0 makes T superharmonic, so its minimum is on the boundary (0 here). The maximum
    // sits on the hole, where T = TH: the source lowers T away from the hole, so no interior node exceeds TH.
    double minT = 1e300;
    for (double t : r.temperatures[0]) minT = std::min(minT, t);
    CHECK(minT > -1e-9);
    CHECK(maxT <= TH + 1e-9);
}

void testMissingFile() {
    std::string err;
    const TriangleMesh m = meshPlanarStepGmsh("this_file_does_not_exist.step", 0.1, err);
    CHECK(m.nodes.empty());
    CHECK(!err.empty());
}

} // namespace

int main() {
    testConvergenceOnCurvedBoundary();
    testStepRoundTrip();
    testPlateWithHoleFromStep();
    testMissingFile();
    if (g_failures == 0) {
        std::cout << "heat2d_gmsh_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_gmsh_test: " << g_failures << " check(s) failed\n";
    return 1;
}
