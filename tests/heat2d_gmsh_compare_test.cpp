/**
 * @file heat2d_gmsh_compare_test.cpp
 * @brief Staircase boundary (structured solver, holes by element centroid) against the conforming Gmsh mesh on the same
 *        plate with a circular hole: 2 x 1 plate, hole of radius 0.3 at (1, 0.5), source 1, outer sides at 0, hole at 50.
 *
 * The quantity is the mean temperature over the solid. The reference is a conforming mesh at h = 0.008, so the errors
 * at the finest levels are of the order of the reference's own error: the test checks orders of convergence and an
 * ordering of the two methods, not absolute values.
 */
#include <OpenModel/Heat2D.hpp>
#include <OpenModel/Heat2DGmsh.hpp>
#include <OpenModel/Heat2DMesh.hpp>

#include <gmsh.h>

#include <cmath>
#include <iostream>
#include <string>
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

constexpr double LX = 2.0;
constexpr double LY = 1.0;
constexpr double CX = 1.0;
constexpr double CY = 0.5;
constexpr double RH = 0.3;
constexpr double TH = 50.0;

OpenModel::Heat1D::Boundary temperatureBoundary(double v) {
    OpenModel::Heat1D::Boundary b;
    b.kind = BoundaryKind::Temperature;
    b.value = [v](double) { return v; };
    return b;
}

/** @brief Mean temperature over the solid of the structured solver, element by element (corner average). */
double structuredMean(int nx, std::string& error) {
    Model m;
    m.lx = LX;
    m.ly = LY;
    m.nx = nx;
    m.ny = nx / 2;
    m.conductivity = [](double, double, double) { return 1.0; };
    m.volumetricHeatCapacity = [](double, double) { return 1.0; };
    m.source = [](double, double, double) { return 1.0; };
    m.left = temperatureBoundary(0.0);
    m.right = temperatureBoundary(0.0);
    m.bottom = temperatureBoundary(0.0);
    m.top = temperatureBoundary(0.0);
    Hole h;
    h.shape = HoleShape::Circle;
    h.cx = CX;
    h.cy = CY;
    h.r = RH;
    m.holes.push_back(h);
    m.hole = temperatureBoundary(TH);
    Options o;
    o.steady = true;
    const Result r = solve(m, o);
    if (!r.error.empty()) {
        error = r.error;
        return 0.0;
    }
    const double hx = LX / m.nx;
    const double hy = LY / m.ny;
    const auto id = [&](int i, int j) { return static_cast<std::size_t>(j * (m.nx + 1) + i); };
    double sum = 0.0;
    double area = 0.0;
    for (int ej = 0; ej < m.ny; ++ej) {
        for (int ei = 0; ei < m.nx; ++ei) {
            const double dx = (ei + 0.5) * hx - CX;
            const double dy = (ej + 0.5) * hy - CY;
            if (dx * dx + dy * dy < RH * RH) continue;  // same centroid rule as the solver
            const double Tavg = 0.25 * (r.temperatures[0][id(ei, ej)] + r.temperatures[0][id(ei + 1, ej)] +
                                        r.temperatures[0][id(ei + 1, ej + 1)] + r.temperatures[0][id(ei, ej + 1)]);
            sum += Tavg * hx * hy;
            area += hx * hy;
        }
    }
    return sum / area;
}

/** @brief Mean temperature over the solid of the conforming mesh at element size h (triangle average). */
double meshMean(const std::string& step, double h, std::size_t& nodes, std::string& error) {
    const TriangleMesh mesh = meshPlanarStepGmsh(step, h, error);
    if (!error.empty()) return 0.0;
    MeshModel mm;
    mm.conductivity = [](double, double, double) { return 1.0; };
    mm.source = [](double, double, double) { return 1.0; };
    mm.boundaries[1] = temperatureBoundary(0.0);
    mm.boundaries[2] = temperatureBoundary(TH);
    Options o;
    o.steady = true;
    const MeshResult r = solveMesh(mesh, mm, o);
    if (!r.error.empty()) {
        error = r.error;
        return 0.0;
    }
    nodes = mesh.nodes.size();
    double sum = 0.0;
    double area = 0.0;
    for (const auto& t : mesh.triangles) {
        const auto& p0 = mesh.nodes[static_cast<std::size_t>(t[0])];
        const auto& p1 = mesh.nodes[static_cast<std::size_t>(t[1])];
        const auto& p2 = mesh.nodes[static_cast<std::size_t>(t[2])];
        const double A = 0.5 * std::abs((p1[0] - p0[0]) * (p2[1] - p0[1]) - (p2[0] - p0[0]) * (p1[1] - p0[1]));
        const double Tavg = (r.temperatures[0][static_cast<std::size_t>(t[0])] + r.temperatures[0][static_cast<std::size_t>(t[1])] +
                             r.temperatures[0][static_cast<std::size_t>(t[2])]) / 3.0;
        sum += Tavg * A;
        area += A;
    }
    return sum / area;
}

void writePlateStep(const std::string& path) {
    gmsh::initialize();
    gmsh::option::setNumber("General.Terminal", 0);
    gmsh::model::add("plate");
    const int rect = gmsh::model::occ::addRectangle(0.0, 0.0, 0.0, LX, LY);
    const int hole = gmsh::model::occ::addDisk(CX, CY, 0.0, RH, RH);
    std::vector<std::pair<int, int>> cut;
    std::vector<std::vector<std::pair<int, int>>> cutMap;
    gmsh::model::occ::cut({{2, rect}}, {{2, hole}}, cut, cutMap);
    gmsh::model::occ::synchronize();
    gmsh::write(path);
    gmsh::finalize();
}

void testStaircaseAgainstConforming() {
    const std::string step = "heat2d_plate_hole_compare.step";
    writePlateStep(step);

    std::string err;
    std::size_t nodes = 0;
    const double ref = meshMean(step, 0.008, nodes, err);
    CHECK(err.empty());

    // Staircase: errors at nx = 40, 80, 160 (h = 0.05, 0.025, 0.0125).
    double e[3] = {0.0, 0.0, 0.0};
    const int nxs[3] = {40, 80, 160};
    for (int k = 0; k < 3; ++k) {
        std::string serr;
        const double mean = structuredMean(nxs[k], serr);
        CHECK(serr.empty());
        e[k] = std::abs(mean - ref);
        std::cout << "staircase nx = " << nxs[k] << ": error " << e[k] << "\n";
    }
    // Conforming: errors at h = 0.05, 0.025, 0.0125.
    const double hs[3] = {0.05, 0.025, 0.0125};
    double c[3] = {0.0, 0.0, 0.0};
    for (int k = 0; k < 3; ++k) {
        std::size_t n = 0;
        std::string merr;
        const double mean = meshMean(step, hs[k], n, merr);
        CHECK(merr.empty());
        c[k] = std::abs(mean - ref);
        std::cout << "conforming h = " << hs[k] << " (" << n << " nodes): error " << c[k] << "\n";
    }

    // Staircase: first order, the error halves when h halves.
    const double staircaseRatio = e[1] / e[2];
    std::cout << "staircase ratio 80 -> 160: " << staircaseRatio << "\n";
    CHECK(staircaseRatio > 1.5 && staircaseRatio < 2.5);
    // Conforming: faster than first order, the error falls by more than 3 when h halves.
    const double conformingRatio = c[1] / c[2];
    std::cout << "conforming ratio 0.025 -> 0.0125: " << conformingRatio << "\n";
    CHECK(conformingRatio > 3.0);
    // The coarsest conforming mesh (h = 0.05, under 1000 nodes) beats the staircase with nx = 160 (about 11000 elements).
    CHECK(c[0] < e[2]);
    // Reference level: the reference itself is a conforming mesh at h = 0.008.
    CHECK(c[2] < 0.01);
}

} // namespace

int main() {
    testStaircaseAgainstConforming();
    if (g_failures == 0) {
        std::cout << "heat2d_gmsh_compare_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_gmsh_compare_test: " << g_failures << " check(s) failed\n";
    return 1;
}
