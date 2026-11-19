/**
 * @file rigid_link_test.cpp
 * @brief Rigid links (RBE2-style hole couplings): circle fit of hole edges, per-hole generation with a reference point,
 * and the CalculiX deck written for them.
 */
#include <OpenModel/AnalysisSetup.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace OpenModel;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n";        \
        }                                                                                  \
    } while (0)

constexpr double kPi = 3.14159265358979323846;

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol; }

/**
 * @brief Points of a ring of radius `radius` around `centre` in the plane normal to `axis` (0 = x, 1 = y, 2 = z).
 * The ring is offset by `shift` along the axis, so a ring at shift 0 lies in the plane through the centre.
 */
std::vector<std::array<double, 3>> ringPoints(const std::array<double, 3>& centre, double radius, int count, int axis,
                                              double shift = 0.0) {
    std::vector<std::array<double, 3>> out;
    const int u = (axis + 1) % 3;
    const int v = (axis + 2) % 3;
    for (int i = 0; i < count; ++i) {
        const double angle = 2.0 * kPi * i / count;
        std::array<double, 3> p = centre;
        p[static_cast<std::size_t>(u)] += radius * std::cos(angle);
        p[static_cast<std::size_t>(v)] += radius * std::sin(angle);
        p[static_cast<std::size_t>(axis)] += shift;
        out.push_back(p);
    }
    return out;
}

/** @brief Mesh holding only the given points as nodes; returns their indices in order. */
MeshData nodesOnly(const std::vector<std::array<double, 3>>& points, std::vector<NodeIndex>& indices) {
    MeshData m;
    for (const auto& p : points) indices.push_back(m.addNode(p[0], p[1], p[2]));
    return m;
}

/** @brief Single tetrahedron with ids 1..4, the same reference element the other solver tests use. */
MeshData tetrahedron() {
    MeshData m;
    m.addNode(1, 0, 0, 0);
    m.addNode(2, 1, 0, 0);
    m.addNode(3, 0, 1, 0);
    m.addNode(4, 0, 0, 1);
    m.addElement("VOL", ElementType::Tetrahedron4, 1, {0, 1, 2, 3});
    std::vector<MeshBody> bodies{MeshBody{"Part", 3, {1}}};
    m.setBodies(bodies);
    return m;
}

bool hasWarning(const std::vector<ImportDiagnostic>& diagnostics, const std::string& code) {
    for (const auto& d : diagnostics)
        if (d.code == code) return true;
    return false;
}

std::string readAll(const std::filesystem::path& p) {
    std::ifstream in(p);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

} // namespace

int main() {
    // 1. Circle in the plane z = 2, centre (5, 5, 2), radius 2, twelve nodes.
    {
        std::vector<NodeIndex> ring;
        const MeshData m = nodesOnly(ringPoints({5.0, 5.0, 2.0}, 2.0, 12, 2), ring);
        HoleFit fit;
        CHECK(fitHoleCircle(m, ring, fit));
        CHECK(near(fit.centre[0], 5.0) && near(fit.centre[1], 5.0) && near(fit.centre[2], 2.0));
        CHECK(near(fit.radius, 2.0));
        CHECK(near(std::fabs(fit.axis[2]), 1.0));
        CHECK(fit.maxDeviation < 1e-9);
    }
    // 2. Circle in the plane x = 3, centre (3, 1, 1), radius 0.5: the axis must be along x.
    {
        std::vector<NodeIndex> ring;
        const MeshData m = nodesOnly(ringPoints({3.0, 1.0, 1.0}, 0.5, 8, 0), ring);
        HoleFit fit;
        CHECK(fitHoleCircle(m, ring, fit));
        CHECK(near(std::fabs(fit.axis[0]), 1.0));
        CHECK(near(fit.radius, 0.5));
    }
    // 3. Fewer than three nodes cannot define a hole.
    {
        std::vector<NodeIndex> two;
        const MeshData m = nodesOnly({{0, 0, 0}, {1, 0, 0}}, two);
        HoleFit fit;
        CHECK(!fitHoleCircle(m, two, fit));
    }
    // 4. Two holes, reference on the axis of the first: no warnings, ids in order, slaves copied.
    {
        std::vector<NodeIndex> ringA, ringB;
        auto pointsA = ringPoints({0.0, 0.0, 0.0}, 1.0, 8, 2);
        auto pointsB = ringPoints({0.0, 0.0, 5.0}, 1.0, 8, 2); // same axis as the first hole, so one reference fits both
        pointsA.insert(pointsA.end(), pointsB.begin(), pointsB.end());
        std::vector<NodeIndex> all;
        const MeshData m = nodesOnly(pointsA, all);
        ringA.assign(all.begin(), all.begin() + 8);
        ringB.assign(all.begin() + 8, all.end());
        std::vector<ImportDiagnostic> diagnostics;
        std::string error;
        const auto links = holeRigidLinks(m, {ringA, ringB}, {0.0, 0.0, 10.0}, diagnostics, error);
        CHECK(error.empty());
        CHECK(links.size() == 2);
        CHECK(links.size() == 2 && links[0].id == "rbe2-1" && links[1].id == "rbe2-2");
        CHECK(links.size() == 2 && links[0].slaves == ringA && links[1].slaves == ringB);
        CHECK(!hasWarning(diagnostics, "RBE2_OFF_AXIS"));
        CHECK(!hasWarning(diagnostics, "RBE2_NOT_CIRCULAR"));
    }
    // 5. Reference far from the axis of the hole: warning, the link is still created.
    {
        std::vector<NodeIndex> ring;
        const MeshData m = nodesOnly(ringPoints({0.0, 0.0, 0.0}, 1.0, 8, 2), ring);
        std::vector<ImportDiagnostic> diagnostics;
        std::string error;
        const auto links = holeRigidLinks(m, {ring}, {3.0, 0.0, 10.0}, diagnostics, error);
        CHECK(links.size() == 1);
        CHECK(hasWarning(diagnostics, "RBE2_OFF_AXIS"));
    }
    // 6. Ring with one node pushed out to twice the radius: warning about the circularity.
    {
        auto points = ringPoints({0.0, 0.0, 0.0}, 1.0, 8, 2);
        points[3][0] *= 2.0;
        points[3][1] *= 2.0;
        std::vector<NodeIndex> ring;
        const MeshData m = nodesOnly(points, ring);
        std::vector<ImportDiagnostic> diagnostics;
        std::string error;
        const auto links = holeRigidLinks(m, {ring}, {0.0, 0.0, 5.0}, diagnostics, error);
        CHECK(links.size() == 1);
        CHECK(hasWarning(diagnostics, "RBE2_NOT_CIRCULAR"));
    }
    // 7. A hole that cannot be fitted makes the whole call fail with a message.
    {
        std::vector<NodeIndex> two;
        const MeshData m = nodesOnly({{0, 0, 0}, {1, 0, 0}}, two);
        std::vector<ImportDiagnostic> diagnostics;
        std::string error;
        const auto links = holeRigidLinks(m, {two}, {0.0, 0.0, 1.0}, diagnostics, error);
        CHECK(links.empty());
        CHECK(!error.empty());
    }
    // 8. The deck: the reference node gets the id after the largest mesh id, the slaves form MG_RBS1, and the
    // *RIGID BODY line refers to the reference node. The mesh itself is not changed.
    {
        const MeshData mesh = tetrahedron();
        AnalysisModel model;
        model.material.youngModulus = 210000.0;
        model.material.poisson = 0.3;
        FixedSupport fix;
        fix.id = "fix";
        fix.where.kind = SelectorKind::Nodes;
        fix.where.nodes = {0};
        model.supports.push_back(fix);
        RigidLink link;
        link.id = "rbe2-1";
        link.reference = {0.5, 0.5, 2.0};
        link.slaves = {1, 2, 3};
        model.rigidLinks.push_back(link);
        AnalysisJob job;
        job.id = "job";
        job.steps.emplace_back();
        model.jobs.push_back(job);

        const auto dir = std::filesystem::temp_directory_path() / "openmodel_rigid_link_test";
        std::filesystem::create_directories(dir);
        const auto r = writeSolverInput(mesh, model, dir / "rbe.inp");
        CHECK(r.success);
        if (!r.success) std::cerr << r.error << "\n";
        const std::string deck = readAll(dir / "rbe.inp");
        CHECK(deck.find("*NSET, NSET=MG_RBS1\n2, 3, 4\n") != std::string::npos);
        CHECK(deck.find("*RIGID BODY, NSET=MG_RBS1, REF NODE=5\n") != std::string::npos);
        // The reference node is written as a node of the mesh file, with its coordinates.
        const auto nodeBlock = deck.find("*NODE");
        CHECK(nodeBlock != std::string::npos);
        CHECK(nodeBlock != std::string::npos && deck.find("\n5,", nodeBlock) != std::string::npos);
        CHECK(mesh.getNumNodes() == 4);
    }
    // 9. A node in two links is rejected before anything is written.
    {
        const MeshData mesh = tetrahedron();
        AnalysisModel model;
        model.material.youngModulus = 210000.0;
        model.material.poisson = 0.3;
        FixedSupport fix;
        fix.id = "fix";
        fix.where.kind = SelectorKind::Nodes;
        fix.where.nodes = {0};
        model.supports.push_back(fix);
        RigidLink a{"a", {1, 1, 1}, {1, 2}};
        RigidLink b{"b", {2, 2, 2}, {2, 3}};
        model.rigidLinks = {a, b};
        AnalysisJob job;
        job.id = "job";
        job.steps.emplace_back();
        model.jobs.push_back(job);
        const auto dir = std::filesystem::temp_directory_path() / "openmodel_rigid_link_test";
        std::filesystem::create_directories(dir);
        const auto r = writeSolverInput(mesh, model, dir / "overlap.inp");
        CHECK(!r.success);
    }

    if (g_failures == 0) std::cout << "rigid_link_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
