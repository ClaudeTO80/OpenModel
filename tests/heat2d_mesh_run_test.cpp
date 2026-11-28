/**
 * @file heat2d_mesh_run_test.cpp
 * @brief End to end: a mesh script that imports a STEP annulus, meshes it with Gmsh and solves it, compared with the same
 *        problem built through the API (meshPlanarStepGmsh + solveMesh). Also checks a missing file, a transient run and
 *        the CSV of the result.
 */
#include <OpenModel/Heat2DGmsh.hpp>
#include <OpenModel/Heat2DMeshScript.hpp>
#include <OpenModel/Heat2DMesh.hpp>

#include <gmsh.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
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

/** @brief Exact radial solution of the annulus problem with source 1, k = 1, outer 0, inner TH. */
double exactAt(double r) {
    const double A = (TH + (A_IN * A_IN - R * R) / 4.0) / std::log(A_IN / R);
    const double B = R * R / 4.0 - A * std::log(R);
    return -r * r / 4.0 + A * std::log(r) + B;
}

/** @brief Writes the annulus of the test to a STEP file (OpenCASCADE kernel). */
void writeAnnulusStep(const std::string& path) {
    gmsh::initialize();
    gmsh::option::setNumber("General.Terminal", 0);
    gmsh::model::add("writer");
    const int outer = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, R, R);
    const int inner = gmsh::model::occ::addDisk(0.0, 0.0, 0.0, A_IN, A_IN);
    std::vector<std::pair<int, int>> cut;
    std::vector<std::vector<std::pair<int, int>>> cutMap;
    gmsh::model::occ::cut({{2, outer}}, {{2, inner}}, cut, cutMap);
    gmsh::model::occ::synchronize();
    gmsh::write(path);
    gmsh::finalize();
}

/** @brief The script of the annulus problem, pointing at the given STEP file. */
std::string annulusScript(const std::string& path, const std::string& step) {
    return "analysis heat 2d mesh\n"
           "geometry step file=" + path + " size=0.05\n"
           "material k=1 rhocp=1\n"
           "boundary tag=1 temperature T=0\n"
           "boundary tag=2 temperature T=50\n"
           "heat q=1\n" + step + "\n";
}

/** @brief The same problem built with the API, for the comparison. */
MeshResult apiSolve(const std::string& path, TriangleMesh& mesh) {
    std::string err;
    mesh = meshPlanarStepGmsh(path, 0.05, err);
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
    return solveMesh(mesh, model, o);
}

/** @brief Largest nodal error on the annulus against the exact solution, for the first recorded time. */
double annulusError(const TriangleMesh& mesh, const MeshResult& r) {
    double worst = 0.0;
    for (std::size_t i = 0; i < mesh.nodes.size(); ++i) {
        const double rr = std::hypot(mesh.nodes[i][0], mesh.nodes[i][1]);
        if (rr < A_IN - 1e-12 || rr > R + 1e-12) continue;
        worst = std::max(worst, std::abs(r.temperatures[0][i] - exactAt(rr)));
    }
    return worst;
}

void testScriptMatchesApi() {
    const std::string path = "heat2d_run_annulus.step";
    writeAnnulusStep(path);

    MeshScriptProblem problem;
    std::vector<std::string> errors;
    CHECK(parseMeshScript(annulusScript(path, "step steady"), problem, errors));
    CHECK(errors.empty());

    TriangleMesh scriptMesh;
    MeshResult scriptResult;
    const std::string runErr = runMeshScript(problem, scriptMesh, scriptResult);
    CHECK(runErr.empty());
    CHECK(scriptResult.converged);

    TriangleMesh apiMesh;
    const MeshResult apiResult = apiSolve(path, apiMesh);
    CHECK(apiResult.error.empty());

    CHECK(scriptMesh.nodes.size() == apiMesh.nodes.size());
    CHECK(scriptMesh.triangles.size() == apiMesh.triangles.size());
    const double scriptErr = annulusError(scriptMesh, scriptResult);
    const double apiErr = annulusError(apiMesh, apiResult);
    std::cout << "script run: " << scriptMesh.nodes.size() << " nodes, max error " << scriptErr << " K; API "
              << apiErr << " K\n";
    CHECK(std::abs(scriptErr - apiErr) < 1e-9);
    CHECK(scriptErr < 0.05);

    // CSV: header, then one row per node for the single recorded time.
    const std::string csv = meshResultToCsv(scriptMesh, scriptResult);
    CHECK(csv.rfind("time,x,y,temperature\n", 0) == 0);
    std::size_t rows = 0;
    for (char ch : csv)
        if (ch == '\n') ++rows;
    CHECK(rows == 1 + scriptMesh.nodes.size());
}

void testTransientRun() {
    const std::string path = "heat2d_run_transient.step";
    writeAnnulusStep(path);
    MeshScriptProblem problem;
    std::vector<std::string> errors;
    CHECK(parseMeshScript(annulusScript(path, "step transient dt=0.01 end=0.02 theta=1 capacity=lumped"), problem, errors));
    CHECK(errors.empty());
    CHECK(!problem.options.steady);
    TriangleMesh mesh;
    MeshResult r;
    const std::string err = runMeshScript(problem, mesh, r);
    CHECK(err.empty());
    CHECK(r.times.size() >= 2);
    CHECK(!r.times.empty() && std::abs(r.times.back() - 0.02) < 1e-9);
    // Starting from 0 with a source and a hot hole, the solid warms: the mean temperature rises from the first to the last
    // recorded time.
    if (r.temperatures.size() >= 2) {
        double first = 0.0, last = 0.0;
        for (double t : r.temperatures.front()) first += t;
        for (double t : r.temperatures.back()) last += t;
        CHECK(last > first);
    }
}

void testMissingStepFile() {
    MeshScriptProblem problem;
    std::vector<std::string> errors;
    CHECK(parseMeshScript(annulusScript("this_file_does_not_exist.step", "step steady"), problem, errors));
    TriangleMesh mesh;
    MeshResult r;
    const std::string err = runMeshScript(problem, mesh, r);
    CHECK(!err.empty());
    CHECK(mesh.nodes.empty());
}

} // namespace

int main() {
    testScriptMatchesApi();
    testTransientRun();
    testMissingStepFile();
    if (g_failures == 0) {
        std::cout << "heat2d_mesh_run_test: all checks passed\n";
        return 0;
    }
    std::cerr << "heat2d_mesh_run_test: " << g_failures << " check(s) failed\n";
    return 1;
}
