/**
 * @file heat2d_mesh_test.cpp
 * @brief Verification of the steady triangle-mesh solver (Heat2DMesh) without Gmsh: a square split into triangles
 *        against the double Fourier reference, the same problem against the structured solver, the input checks, and
 *        the refusal of transient runs.
 */
#include <OpenModel/Heat2D.hpp>
#include <OpenModel/Heat2DMesh.hpp>

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

/** @brief Unit square split into 2 n^2 triangles; the four sides get tags 1 (left), 2 (right), 3 (bottom), 4 (top). */
TriangleMesh squareMesh(int n) {
    TriangleMesh m;
    const int nn = n + 1;
    m.nodes.resize(static_cast<std::size_t>(nn) * nn);
    auto id = [nn](int i, int j) { return j * nn + i; };
    for (int j = 0; j <= n; ++j)
        for (int i = 0; i <= n; ++i)
            m.nodes[static_cast<std::size_t>(id(i, j))] = {static_cast<double>(i) / n, static_cast<double>(j) / n};
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            m.triangles.push_back({id(i, j), id(i + 1, j), id(i + 1, j + 1)});
            m.triangles.push_back({id(i, j), id(i + 1, j + 1), id(i, j + 1)});
        }
    }
    for (int j = 0; j < n; ++j) {
        m.edges.push_back({id(0, j), id(0, j + 1), 1});
        m.edges.push_back({id(n, j), id(n, j + 1), 2});
    }
    for (int i = 0; i < n; ++i) {
        m.edges.push_back({id(i, 0), id(i + 1, 0), 3});
        m.edges.push_back({id(i, n), id(i + 1, n), 4});
    }
    return m;
}

OpenModel::Heat1D::Boundary temperatureBoundary(double v) {
    OpenModel::Heat1D::Boundary b;
    b.kind = BoundaryKind::Temperature;
    b.value = [v](double) { return v; };
    return b;
}

MeshModel zeroSidesWithSource(double sourceValue) {
    MeshModel model;
    model.conductivity = [](double, double, double) { return 1.0; };
    model.source = [sourceValue](double, double, double) { return sourceValue; };
    for (int tag = 1; tag <= 4; ++tag) model.boundaries[tag] = temperatureBoundary(0.0);
    return model;
}

Options steadyOptions() {
    Options o;
    o.steady = true;
    return o;
}

void testFourierOnTriangles() {
    // -lap(T) = 1 on the unit square, zero on the sides: the reference of heat2d_test.
    const TriangleMesh mesh = squareMesh(40);
    const MeshResult r = solveMesh(mesh, zeroSidesWithSource(1.0), steadyOptions());
    CHECK(r.error.empty());
    CHECK(r.converged);
    double sum = 0.0;
    for (int m = 1; m < 200; m += 2) {
        for (int n = 1; n < 200; n += 2) {
            const double sgn = (((m - 1) / 2 + (n - 1) / 2) % 2 == 0) ? 1.0 : -1.0;
            sum += sgn * 16.0 / (kPi * kPi * kPi * kPi * m * n * (static_cast<double>(m) * m + static_cast<double>(n) * n));
        }
    }
    // The centre is the node (20, 20) of the 41 x 41 grid.
    const double centre = r.temperatures[static_cast<std::size_t>(20 * 41 + 20)];
    std::cout << "triangle mesh centre " << centre << "  Fourier " << sum << "  error " << std::abs(centre - sum) << "\n";
    CHECK(std::abs(centre - sum) < 1e-4);
}

void testAgainstStructuredSolver() {
    // The same problem on the structured quadrilateral solver (plain 40 x 40 plate, same source and sides).
    Model m;
    m.lx = 1.0;
    m.ly = 1.0;
    m.nx = 40;
    m.ny = 40;
    m.conductivity = [](double, double, double) { return 1.0; };
    m.volumetricHeatCapacity = [](double, double) { return 1.0; };
    m.source = [](double, double, double) { return 1.0; };
    m.left = temperatureBoundary(0.0);
    m.right = temperatureBoundary(0.0);
    m.bottom = temperatureBoundary(0.0);
    m.top = temperatureBoundary(0.0);
    const Result sr = solve(m, steadyOptions());
    CHECK(sr.error.empty());
    const MeshResult tr = solveMesh(squareMesh(40), zeroSidesWithSource(1.0), steadyOptions());
    CHECK(tr.error.empty());
    // Both are second-order discretisations of the same problem: their centres differ by the discretisation error only.
    const double quadCentre = sr.temperatures[0][static_cast<std::size_t>(20 * 41 + 20)];
    const double triCentre = tr.temperatures[static_cast<std::size_t>(20 * 41 + 20)];
    std::cout << "structured centre " << quadCentre << "  triangles centre " << triCentre << "\n";
    CHECK(std::abs(quadCentre - triCentre) < 1e-4);
}

void testInputChecks() {
    // Transient runs are refused.
    Options transient;
    transient.steady = false;
    transient.timeStep = 0.1;
    transient.endTime = 1.0;
    const MeshResult tr = solveMesh(squareMesh(4), zeroSidesWithSource(1.0), transient);
    CHECK(tr.error.find("only the steady") != std::string::npos);

    // Triangle that refers to a missing node.
    TriangleMesh bad = squareMesh(2);
    bad.triangles[0][0] = 999;
    CHECK(validateMesh(bad, zeroSidesWithSource(1.0)).find("does not exist") != std::string::npos);

    // Degenerate triangle: three collinear nodes.
    TriangleMesh flat = squareMesh(2);
    flat.nodes[0] = {0.5, 0.0};
    flat.triangles = {{0, 1, 2}};
    CHECK(validateMesh(flat, zeroSidesWithSource(1.0)).find("zero area") != std::string::npos);

    // Steady problem with no temperature and no convection: undetermined level.
    MeshModel insulated;
    insulated.conductivity = [](double, double, double) { return 1.0; };
    const MeshResult nk = solveMesh(squareMesh(3), insulated, steadyOptions());
    CHECK(nk.error.find("undetermined") != std::string::npos);

    // Two temperature boundaries meeting at a node with different values.
    TriangleMesh clash = squareMesh(2);
    MeshModel mismatched = zeroSidesWithSource(1.0);
    mismatched.boundaries[3] = temperatureBoundary(10.0);
    const MeshResult cr = solveMesh(clash, mismatched, steadyOptions());
    CHECK(cr.error.find("different temperatures") != std::string::npos);
}

} // namespace

int main() {
    testFourierOnTriangles();
    testAgainstStructuredSolver();
    testInputChecks();

    if (g_failures == 0) {
        std::cout << "heat2d_mesh_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_mesh_test: " << g_failures << " check(s) failed\n";
    return 1;
}
