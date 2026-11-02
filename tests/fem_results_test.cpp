#include <OpenModel/FemResults.hpp>
#include <OpenModel/ResultBinding.hpp>
#include <OpenModel/WebViewWriter.hpp>

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace OpenModel;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                           \
    do {                                                                                      \
        if (!(cond)) {                                                                        \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n";    \
            ++g_failures;                                                                     \
        }                                                                                     \
    } while (0)

bool near(double a, double b, double tol = 1e-9) { return std::fabs(a - b) <= tol * std::max(1.0, std::fabs(b)); }

bool hasCode(const std::vector<ImportDiagnostic>& d, const std::string& code) {
    for (const auto& x : d)
        if (x.code == code) return true;
    return false;
}

/* ------------------------------------------------------------ Fortran-style FRD writer */

std::string e12(double v) {
    char b[40];
    std::snprintf(b, sizeof b, "%12.5E", v);
    return b;
}
std::string iN(long long n, int width) {
    char b[40];
    std::snprintf(b, sizeof b, "%*lld", width, n);
    return b;
}
std::string padRight(std::string s, std::size_t width) {
    s.resize(width, ' ');
    return s;
}

struct FrdWriter {
    int format = 1;  // 0 short (I5 node), 1 long (I10 node)
    std::string text;

    void line(const std::string& s) { text += s + "\n"; }
    void header() {
        line("    1CTEST");
        line("    1UDATE  26.10.2026");
    }
    void nodes(const MeshData& mesh, double shift = 0.0, NodeId idOffset = 0) {
        line("    2C" + std::string(6, ' ') + iN(static_cast<long long>(mesh.getNumNodes()), 12) + std::string(37, ' ') + std::to_string(format));
        for (std::size_t n = 0; n < mesh.getNumNodes(); ++n) {
            const auto c = mesh.nodeCoordinates(n);
            line(" -1" + iN(static_cast<long long>(mesh.nodeId(n) + idOffset), format == 0 ? 5 : 10) + e12(c[0] + shift) + e12(c[1]) + e12(c[2]));
        }
        line(" -3");
    }
    void elementsStub() {
        line("    3C" + std::string(6, ' ') + iN(1, 12) + std::string(37, ' ') + std::to_string(format));
        line(" -1" + iN(1, 10) + iN(4, 5) + iN(0, 5) + iN(1, 5));
        line(" -2" + iN(1, 10) + iN(2, 10) + iN(3, 10) + iN(4, 10));
        line(" -3");
    }
    void resultHeader(double value, long long numNodes, int ictype, int step) {
        line("  100CL  101" + e12(value) + iN(numNodes, 12) + std::string(20, ' ') + iN(ictype, 2) + iN(step, 5) + std::string(10, ' ') + iN(format, 2));
    }
    void dataset(const std::string& name, int ncomps, int irtype) { line(" -4  " + padRight(name, 8) + iN(ncomps, 5) + iN(irtype, 5)); }
    void component(const std::string& name, int type, int c1, int c2, int exist, const std::string& calc = "") {
        line(" -5  " + padRight(name, 8) + iN(1, 5) + iN(type, 5) + iN(c1, 5) + iN(c2, 5) + iN(exist, 5) + calc);
    }
    void record(long long node, const std::vector<std::string>& cells) {
        std::string first = " -1" + iN(node, format == 0 ? 5 : 10);
        std::size_t i = 0;
        for (; i < cells.size() && i < 6; ++i) first += cells[i];
        line(first);
        while (i < cells.size()) {
            std::string cont = " -2" + std::string(format == 0 ? 5 : 10, ' ');
            for (std::size_t k = 0; k < 6 && i < cells.size(); ++k, ++i) cont += cells[i];
            line(cont);
        }
    }
    void end() { line(" -3"); }
    void finish() { line(" 9999"); }
};

/** Unit cube split into tetrahedra: ids 1001.., coordinates scaled so they are not trivial. */
MeshData makeMesh() {
    MeshData mesh;
    const std::size_t n = 2, s = n + 1;
    auto idx = [s](std::size_t x, std::size_t y, std::size_t z) { return static_cast<NodeIndex>((z * s + y) * s + x); };
    for (std::size_t z = 0; z <= n; ++z)
        for (std::size_t y = 0; y <= n; ++y)
            for (std::size_t x = 0; x <= n; ++x) mesh.addNode(1001 + idx(x, y, z), 0.5 * double(x) + 10.0, 0.25 * double(y), 0.125 * double(z));
    ElementId id = 1;
    for (std::size_t z = 0; z < n; ++z)
        for (std::size_t y = 0; y < n; ++y)
            for (std::size_t x = 0; x < n; ++x) {
                const NodeIndex a = idx(x, y, z), b = idx(x + 1, y, z), c = idx(x, y + 1, z), d = idx(x + 1, y + 1, z);
                const NodeIndex e = idx(x, y, z + 1), f = idx(x + 1, y, z + 1), g = idx(x, y + 1, z + 1), h = idx(x + 1, y + 1, z + 1);
                const NodeIndex t[6][4] = {{a, b, d, h}, {a, d, c, h}, {a, c, g, h}, {a, g, e, h}, {a, e, f, h}, {a, f, b, h}};
                for (const auto& q : t) mesh.addElement("tet", ElementType::Tetrahedron4, id++, {q[0], q[1], q[2], q[3]});
            }
    return mesh;
}

/** Result file for makeMesh(): DISP = (0.01 x, -0.002 y, 0), STRESS = uniaxial 100 in x, two steps. */
std::string makeResultFile(const MeshData& mesh, int format = 1, double coordinateShift = 0.0, NodeId idOffset = 0) {
    FrdWriter w;
    w.format = format;
    w.header();
    w.nodes(mesh, coordinateShift, idOffset);
    w.elementsStub();
    for (int step = 1; step <= 2; ++step) {
        const double t = 0.5 * step, scale = step;  // second step doubles everything
        w.resultHeader(t, static_cast<long long>(mesh.getNumNodes()), 1, step);
        w.dataset("DISP", 4, 1);
        w.component("D1", 2, 1, 0, 0);
        w.component("D2", 2, 2, 0, 0);
        w.component("D3", 2, 3, 0, 0);
        w.component("ALL", 2, 0, 0, 1, "ALL");
        for (std::size_t n = 0; n < mesh.getNumNodes(); ++n) {
            const auto c = mesh.nodeCoordinates(n);
            w.record(static_cast<long long>(mesh.nodeId(n) + idOffset), {e12(scale * 0.01 * c[0]), e12(scale * -0.002 * c[1]), e12(0.0)});
        }
        w.end();
        w.resultHeader(t, static_cast<long long>(mesh.getNumNodes()), 1, step);
        w.dataset("STRESS", 6, 1);
        for (const char* name : {"SXX", "SYY", "SZZ", "SXY", "SYZ", "SZX"}) w.component(name, 4, 1, 1, 0);
        for (std::size_t n = 0; n < mesh.getNumNodes(); ++n)
            w.record(static_cast<long long>(mesh.nodeId(n) + idOffset), {e12(100.0 * scale), e12(0.0), e12(0.0), e12(0.0), e12(0.0), e12(0.0)});
        w.end();
    }
    w.finish();
    return w.text;
}

ResultImportResult parseFrd(const std::string& text, FemResults& out) {
    std::istringstream in(text);
    return CalculixFrdImporter{}.parse(in, out);
}
ResultImportResult parseCsv(const std::string& text, FemResults& out) {
    std::istringstream in(text);
    return CsvResultImporter{}.parse(in, out);
}

const ResultField* findField(const FemResults& r, const std::string& name, std::size_t step) {
    for (const auto& f : r.fields)
        if (f.name == name && f.step == step) return &f;
    return nullptr;
}
const WebNodalField* findView(const WebViewOptions& v, const std::string& name) {
    for (const auto& f : v.fields)
        if (f.name == name) return &f;
    return nullptr;
}

/* ------------------------------------------------------------------------- tests */

void testFrdParsing() {
    const MeshData mesh = makeMesh();
    FemResults r;
    auto res = parseFrd(makeResultFile(mesh), r);
    CHECK(res);
    CHECK(r.format == "CalculiX FRD");
    CHECK(r.steps.size() == 2);
    CHECK(r.fields.size() == 4);  // DISP, STRESS for two steps
    CHECK(r.modelNodeIds.size() == mesh.getNumNodes());
    CHECK(near(r.modelCoordinates[3][0], mesh.nodeCoordinates(3)[0], 1e-5));

    const auto* disp = findField(r, "DISP", 0);
    CHECK(disp != nullptr);
    if (disp) {
        CHECK(disp->componentCount() == 3);  // the calculated ALL entity is not stored
        CHECK(disp->components[0] == "D1" && disp->components[2] == "D3");
        CHECK(disp->recordCount() == mesh.getNumNodes());
        CHECK(disp->ids[0] == 1001);
        const double x = mesh.nodeCoordinates(5)[0];
        CHECK(near(disp->values[5 * 3 + 0], 0.01 * x, 1e-5));
    }
    const auto* disp2 = findField(r, "DISP", 1);
    CHECK(disp2 && near(disp2->values[5 * 3], 2 * disp->values[5 * 3], 1e-9));
    CHECK(r.steps[0].label != r.steps[1].label);
    CHECK(r.steps[1].value == 1.0);

    // Short format: node numbers in I5.
    FemResults shortFormat;
    CHECK(parseFrd(makeResultFile(mesh, 0), shortFormat));
    CHECK(shortFormat.fields.size() == 4 && shortFormat.fields[0].ids[3] == 1004);
}

void testFrdNumbersAndContinuation() {
    FrdWriter w;
    w.header();
    w.resultHeader(0.0, 3, 0, 1);
    w.dataset("WIDE", 8, 1);
    for (int c = 1; c <= 8; ++c) w.component("C" + std::to_string(c), 1, c, 0, 0);
    w.record(7, {e12(1), e12(2), e12(3), e12(4), e12(5), e12(6), e12(7), e12(8)});  // needs a -2 line
    w.record(8, {e12(-1), "         NaN", "************", " 1.23456-101", e12(0), e12(0), e12(0), e12(-8)});
    w.record(9, {e12(9), e12(9), e12(9), e12(9), e12(9), e12(9), e12(9), e12(9)});
    w.end();
    w.finish();
    FemResults r;
    auto res = parseFrd(w.text, r);
    CHECK(res);
    CHECK(r.fields.size() == 1 && r.fields[0].componentCount() == 8);
    if (!r.fields.empty()) {
        const auto& f = r.fields[0];
        CHECK(f.recordCount() == 3);
        CHECK(near(f.values[6], 7) && near(f.values[7], 8));          // continuation line
        CHECK(std::isnan(f.values[8 + 1]));                            // "NaN"
        CHECK(std::isnan(f.values[8 + 2]));                            // overflow stars
        CHECK(near(f.values[8 + 3], 1.23456e-101, 1e-9));              // Fortran 3-digit exponent without 'E'
        CHECK(near(f.values[8 + 7], -8));
        CHECK(f.ids[2] == 9);
    }
}

/**
 * Lines quoted from real CalculiX output (the docstrings of the ccx2paraview reader): header values written with
 * an F format, an analysis text in the header, component records with four integers, dataset names with blanks,
 * adjacent negative numbers and continuation lines. The blocks are assembled from those lines, not whole files.
 */
void testFrdRealWorldLines() {
    const std::string text =
        "    1CTEST\n"
        "  100CL  101 1.000000000         803                     0    1           1\n"
        " -4  DISP        4    1\n"
        " -5  D1          1    2    1    0\n"
        " -5  D2          1    2    2    0\n"
        " -5  D3          1    2    3    0\n"
        " -5  ALL         1    2    0    0    1ALL\n"
        " -1         1-6.93889E-18-9.95185E-01-4.66908E-34\n"
        " -1         2-1.94151E-01-9.76063E-01 6.46011E-30\n"
        " -3\n"
        "  100CL  102 117547.9305          90                     2    2MODAL      1\n"
        " -4  STRESS      6    1\n"
        " -5  SXX         1    4    1    1\n"
        " -5  SYY         1    4    2    2\n"
        " -5  SZZ         1    4    3    3\n"
        " -5  SXY         1    4    1    2\n"
        " -5  SYZ         1    4    2    3\n"
        " -5  SZX         1    4    3    1\n"
        " -1         1 1.47281E+04 1.39140E+04 2.80480E+04 5.35318E+04 6.36642E+03 1.82617E+03\n"
        " -3\n"
        "  100CL  103 0.36028E+01           1                     3    1           1\n"
        " -4  DOR1  Rx    4    1\n"
        " -5  RX          1    1    1    0\n"
        " -5  RY          1    1    2    0\n"
        " -1         5 1.00000E+00-2.00000E+00\n"
        " -3\n"
        " 9999\n";
    FemResults r;
    const auto res = parseFrd(text, r);
    CHECK(res);
    CHECK(r.steps.size() == 3);
    CHECK(r.fields.size() == 3);
    if (r.fields.size() != 3) return;
    CHECK(res.diagnostics.size() >= 2);  // the header announces 803 and 90 nodes, the blocks hold 2 and 1 records
    CHECK(hasCode(res.diagnostics, "FRD_NODE_COUNT"));
    CHECK(r.fields[0].componentCount() == 3 && r.fields[0].recordCount() == 2);
    CHECK(near(r.fields[0].values[0], -6.93889e-18, 1e-6) && near(r.fields[0].values[1], -9.95185e-01) && near(r.fields[0].values[2], -4.66908e-34, 1e-6));
    CHECK(r.steps[0].value == 1.0 && r.steps[1].value == 117547.9305 && near(r.steps[2].value, 3.6028));
    CHECK(r.steps[1].kind == StepKind::Frequency && r.steps[1].mode == 1 && r.steps[1].label.find("mode 1") != std::string::npos);  // ICTYPE 2
    CHECK(r.modeSteps().size() == 1 && r.modeSteps()[0] == 1);
    CHECK(r.fields[1].name == "STRESS" && r.fields[1].componentCount() == 6 && near(r.fields[1].values[5], 1.82617e+03));
    CHECK(r.fields[1].components[3] == "SXY");
    CHECK(r.fields[2].name == "DOR1  Rx" && r.fields[2].components[1] == "RY" && near(r.fields[2].values[1], -2.0));
}

void testFrdErrors() {
    const MeshData mesh = makeMesh();
    FemResults r;
    // Binary files are refused explicitly.
    {
        FrdWriter w;
        w.header();
        w.format = 2;
        w.resultHeader(0.0, 1, 0, 1);
        w.finish();
        auto res = parseFrd(w.text, r);
        CHECK(!res && res.status == ImportStatus::UnsupportedFeature && hasCode(res.diagnostics, "FRD_BINARY"));
    }
    // A record with too few values is an error, not a silent zero.
    {
        FrdWriter w;
        w.header();
        w.resultHeader(0.0, 1, 0, 1);
        w.dataset("DISP", 3, 1);
        for (int c = 1; c <= 3; ++c) w.component("D" + std::to_string(c), 2, c, 0, 0);
        w.record(1, {e12(1), e12(2)});
        w.end();
        w.finish();
        auto res = parseFrd(w.text, r);
        CHECK(!res && hasCode(res.diagnostics, "FRD_TRUNCATED_RECORD"));
    }
    // Element data at nodes (IRTYPE 3) is skipped with a warning; the nodal block is still read.
    {
        FrdWriter w;
        w.header();
        w.resultHeader(0.0, 1, 0, 1);
        w.dataset("ELDAT", 1, 3);
        w.component("E1", 1, 1, 0, 0);
        w.record(1, {e12(1)});
        w.end();
        w.resultHeader(0.0, 1, 0, 1);
        w.dataset("NDTEMP", 1, 1);
        w.component("T", 1, 1, 0, 0);
        w.record(1, {e12(20)});
        w.end();
        w.finish();
        FemResults partial;
        auto res = parseFrd(w.text, partial);
        CHECK(res && hasCode(res.diagnostics, "FRD_DATASET_SKIPPED"));
        CHECK(partial.fields.size() == 1 && partial.fields[0].name == "NDTEMP");
    }
    // No results at all.
    {
        FrdWriter w;
        w.header();
        w.nodes(mesh);
        w.finish();
        auto res = parseFrd(w.text, r);
        CHECK(!res && hasCode(res.diagnostics, "FRD_NO_RESULTS"));
    }
    // A failed import leaves the destination untouched.
    {
        FemResults kept;
        CHECK(parseFrd(makeResultFile(mesh), kept));
        const std::size_t fields = kept.fields.size();
        CHECK(!parseFrd("garbage\n", kept));
        CHECK(kept.fields.size() == fields);
    }
}

void testCsv() {
    FemResults r;
    // Plain comma table with scalar columns, a vector and a missing cell.
    auto res = parseCsv("# comment\nnode,T,U_x,U_y,U_z\n1,20.5,0.1,0.2,0.3\n2,,0.0,0.0,0.5\n", r);
    CHECK(res);
    CHECK(r.steps.size() == 1);
    CHECK(r.fields.size() == 2);  // T and the grouped vector U
    const auto* t = findField(r, "T", 0);
    const auto* u = findField(r, "U", 0);
    CHECK(t && t->componentCount() == 1 && near(t->values[0], 20.5) && std::isnan(t->values[1]));
    CHECK(u && u->componentCount() == 3 && near(u->values[2], 0.3) && near(u->values[5], 0.5) && u->ids[1] == 2);

    // Semicolon with decimal commas and a UTF-8 BOM (spreadsheet export).
    FemResults eu;
    res = parseCsv("\xEF\xBB\xBFNode_ID;Temp\n10;1,5\n11;2,25\n", eu);
    CHECK(res && eu.fields.size() == 1 && near(eu.fields[0].values[1], 2.25) && eu.fields[0].ids[0] == 10);

    // Tensor columns.
    FemResults tensor;
    res = parseCsv("id,S_xx,S_yy,S_zz,S_xy,S_yz,S_zx\n1,1,2,3,4,5,6\n", tensor);
    CHECK(res && tensor.fields.size() == 1 && tensor.fields[0].name == "S" && tensor.fields[0].componentCount() == 6 &&
          near(tensor.fields[0].values[5], 6));

    // Errors.
    CHECK(!parseCsv("a,b\n1,2\n", r) && hasCode(parseCsv("a,b\n1,2\n", r).diagnostics, "CSV_NO_NODE_COLUMN"));
    CHECK(hasCode(parseCsv("node,v\n1,2\n1,3\n", r).diagnostics, "CSV_DUPLICATE_NODE"));
    CHECK(hasCode(parseCsv("node,v\n1,2,3\n", r).diagnostics, "CSV_ROW_WIDTH"));
    CHECK(hasCode(parseCsv("node,v\n1,abc\n", r).diagnostics, "CSV_BAD_VALUE"));
    CHECK(hasCode(parseCsv("node,v\n0,1\n", r).diagnostics, "CSV_BAD_NODE"));
    CHECK(hasCode(parseCsv("node,v\n1.5,1\n", r).diagnostics, "CSV_BAD_NODE"));
    CHECK(hasCode(parseCsv("node,v\n", r).diagnostics, "CSV_NO_ROWS"));
    CHECK(!parseCsv("", r));
    // Reports the offending line number.
    const auto bad = parseCsv("node,v\n1,2\n2,oops\n", r);
    CHECK(!bad.diagnostics.empty() && bad.diagnostics.back().line == 3);

    // Extension dispatch.
    CHECK(readResults("missing.xyz", r).status == ImportStatus::UnsupportedFormat);
    CHECK(readResults("does-not-exist.frd", r).status == ImportStatus::IoError);
}

void testBinding() {
    const MeshData mesh = makeMesh();
    FemResults r;
    CHECK(parseFrd(makeResultFile(mesh), r));

    WebViewOptions view;
    auto b = bindResults(mesh, r, BindOptions{}, view);
    CHECK(b.success);
    CHECK(b.step == 1);  // last step by default
    // Derived fields first, then components: DISP magnitude, DISP.D1..3, six STRESS invariants, STRESS.SXX..SZX.
    CHECK(view.fields.size() == 3 + 1 + 6 + 6 && b.fieldsBound == view.fields.size());
    CHECK(view.fields[0].name == "DISP magnitude");
    const auto* mises = findView(view, "STRESS von Mises");
    CHECK(mises != nullptr);
    if (mises) {
        CHECK(mises->values.size() == mesh.getNumNodes());
        CHECK(near(mises->values[0], 200.0));  // step 2: sxx = 200, all others 0
    }
    const auto* magnitude = findView(view, "DISP magnitude");
    const auto* d1 = findView(view, "DISP.D1");
    CHECK(magnitude && d1);
    if (magnitude && d1) {
        const auto c = mesh.nodeCoordinates(10);
        const double ex = 2 * 0.01 * c[0], ey = 2 * -0.002 * c[1];
        CHECK(near(d1->values[10], ex, 1e-5));
        CHECK(near(magnitude->values[10], std::sqrt(ex * ex + ey * ey), 1e-5));
    }
    CHECK(b.hasDisplacement && b.displacementName == "DISP" && view.displacement.size() == mesh.getNumNodes());
    CHECK(near(view.displacement[10][0], 2 * 0.01 * mesh.nodeCoordinates(10)[0], 1e-5));
    CHECK(view.displacement[10][2] == 0.0);
    CHECK(view.resultsLabel.find("CalculiX FRD") != std::string::npos);

    // First step, no derived fields, no displacement, custom source name.
    WebViewOptions plain;
    BindOptions options;
    options.step = 0;
    options.derivedFields = false;
    options.displacementField = "none";
    options.sourceName = "run1.frd";
    b = bindResults(mesh, r, options, plain);
    CHECK(b.success && b.step == 0 && plain.fields.size() == 3 + 6 && plain.displacement.empty() && !b.hasDisplacement);
    CHECK(plain.resultsLabel.rfind("run1.frd", 0) == 0);
    {
        const auto* sxx = findView(plain, "STRESS.SXX");
        CHECK(sxx && near(sxx->values[0], 100.0));
    }

    // Step out of range, unknown displacement field.
    WebViewOptions untouched;
    options = BindOptions{};
    options.step = 9;
    b = bindResults(mesh, r, options, untouched);
    CHECK(!b.success && !b.error.empty() && untouched.fields.empty());
    options = BindOptions{};
    options.displacementField = "nothing";
    b = bindResults(mesh, r, options, untouched);
    CHECK(!b.success && untouched.fields.empty() && !untouched.displacement.size());

    // Results of another model: shifted coordinates or other node numbers are rejected, never painted on the wrong nodes.
    FemResults shifted;
    CHECK(parseFrd(makeResultFile(mesh, 1, /*shift=*/5.0), shifted));
    b = bindResults(mesh, shifted, BindOptions{}, untouched);
    CHECK(!b.success && b.error.find("different mesh") != std::string::npos && untouched.fields.empty());
    FemResults renumbered;
    CHECK(parseFrd(makeResultFile(mesh, 1, 0.0, /*idOffset=*/5000), renumbered));
    b = bindResults(mesh, renumbered, BindOptions{}, untouched);
    CHECK(!b.success && b.error.find("different model") != std::string::npos);
    options = BindOptions{};
    options.checkCoordinates = false;
    b = bindResults(mesh, shifted, options, untouched);
    CHECK(b.success);  // explicit opt-out
}

void testBindingPartialData() {
    const MeshData mesh = makeMesh();
    // CSV covering half of the nodes plus one id that is not in the mesh.
    std::string csv = "node,T\n";
    for (NodeId id = 1001; id <= 1013; ++id) csv += std::to_string(id) + "," + std::to_string(100 + id - 1001) + "\n";
    csv += "9999,5\n";
    FemResults r;
    CHECK(parseCsv(csv, r));
    WebViewOptions view;
    const auto b = bindResults(mesh, r, BindOptions{}, view);
    CHECK(b.success);
    CHECK(b.unmatchedRecords == 1 && b.nodesWithoutData == mesh.getNumNodes() - 13);
    CHECK(hasCode(b.diagnostics, "BIND_UNMATCHED") && hasCode(b.diagnostics, "BIND_MISSING"));
    CHECK(view.fields.size() == 1 && view.fields[0].name == "T");
    CHECK(near(view.fields[0].values[12], 112.0));
    CHECK(near(view.fields[0].values[20], 100.0));  // node without data takes the field minimum
    for (double v : view.fields[0].values) CHECK(std::isfinite(v));
    CHECK(!b.hasDisplacement);

    // Nothing in common -> error.
    FemResults other;
    CHECK(parseCsv("node,T\n7777,1\n", other));
    WebViewOptions unchanged;
    CHECK(!bindResults(mesh, other, BindOptions{}, unchanged).success && unchanged.fields.empty());

    // CSV displacement vector is recognised under the name U; missing entries are zero displacement.
    FemResults withU;
    CHECK(parseCsv("node,U_x,U_y,U_z\n1001,0.5,0,0\n1002,,1,0\n", withU));
    WebViewOptions vu;
    const auto bu = bindResults(mesh, withU, BindOptions{}, vu);
    CHECK(bu.success && bu.hasDisplacement && bu.displacementName == "U");
    CHECK(near(vu.displacement[0][0], 0.5) && vu.displacement[1][0] == 0.0 && near(vu.displacement[1][1], 1.0));
}

std::uint32_t le32(const unsigned char* p) {
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}

void testWebViewIntegration() {
    const MeshData mesh = makeMesh();
    FemResults r;
    CHECK(parseFrd(makeResultFile(mesh), r));
    WebViewOptions view;
    CHECK(bindResults(mesh, r, BindOptions{}, view).success);
    std::vector<unsigned char> image;
    const auto written = WebViewWriter::build(mesh, view, image);
    CHECK(written.success);
    if (!written.success) { std::cerr << written.error << "\n"; return; }
    const std::size_t jsonSize = le32(&image[12]);
    const std::string json(reinterpret_cast<const char*>(&image[24]), jsonSize);
    const unsigned char* bin = image.data() + 24 + jsonSize;
    CHECK(json.find("\"displacement\":{\"offset\":") != std::string::npos);
    CHECK(json.find("\"deformation\":{\"name\":\"DISP\"") != std::string::npos);
    CHECK(json.find("\"results\":{\"label\":\"CalculiX FRD") != std::string::npos);
    CHECK(json.find("STRESS von Mises") != std::string::npos);
    // The displacement chunk holds 3 float32 per stored vertex.
    const auto k = json.find("\"displacement\":{\"offset\":");
    const std::size_t offset = std::stoul(json.substr(k + 25));
    const auto countKey = json.find("\"count\":", k);
    const std::size_t count = std::stoul(json.substr(countKey + 8));
    CHECK(count == 3 * written.vertices);
    float first[3];
    std::memcpy(first, bin + offset, sizeof first);
    CHECK(first[0] > 0.1999f && first[0] < 0.2201f);  // 2 * 0.01 * x with x in [10, 11]
    // The original MGWV without displacement stays free of the new keys (format is backward compatible).
    std::vector<unsigned char> plain;
    CHECK(WebViewWriter::build(mesh, WebViewOptions{}, plain).success);
    const std::string plainJson(reinterpret_cast<const char*>(&plain[24]), le32(&plain[12]));
    CHECK(plainJson.find("displacement") == std::string::npos && plainJson.find("\"results\"") == std::string::npos);

    // Writer validation of the new option.
    WebViewOptions bad;
    bad.displacement.assign(mesh.getNumNodes() - 1, {0, 0, 0});
    CHECK(!WebViewWriter::build(mesh, bad, image).success);
    bad.displacement.assign(mesh.getNumNodes(), {0, 0, 0});
    bad.displacement[3][1] = std::nan("");
    CHECK(!WebViewWriter::build(mesh, bad, image).success);
}


/* ---------------------------------------------------------------- tensors, elements, modes */

/** Text table with one row per entity id; `rows[i]` holds the values of the columns after the id. */
std::string tableText(const std::string& header, const std::vector<std::uint64_t>& ids, const std::vector<std::vector<double>>& rows) {
    std::ostringstream t;
    t.precision(17);
    t << header << "\n";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        t << ids[i];
        for (const double v : rows[i]) t << "," << v;
        t << "\n";
    }
    return t.str();
}

std::vector<std::uint64_t> allNodeIds(const MeshData& mesh) {
    std::vector<std::uint64_t> ids;
    for (std::size_t n = 0; n < mesh.getNumNodes(); ++n) ids.push_back(mesh.nodeId(n));
    return ids;
}

/** Rotates diag(a,b,c) by angle `t` about the unit axis `u`: returns xx,yy,zz,xy,yz,zx. */
std::vector<double> rotatedDiagonal(double a, double b, double c, double t, double ux, double uy, double uz) {
    const double n = std::sqrt(ux * ux + uy * uy + uz * uz);
    ux /= n; uy /= n; uz /= n;
    const double co = std::cos(t), si = std::sin(t), k = 1 - co;
    const double R[3][3] = {{co + ux * ux * k, ux * uy * k - uz * si, ux * uz * k + uy * si},
                            {uy * ux * k + uz * si, co + uy * uy * k, uy * uz * k - ux * si},
                            {uz * ux * k - uy * si, uz * uy * k + ux * si, co + uz * uz * k}};
    const double d[3] = {a, b, c};
    double m[3][3] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int q = 0; q < 3; ++q) m[i][j] += R[i][q] * d[q] * R[j][q];
    return {m[0][0], m[1][1], m[2][2], m[0][1], m[1][2], m[2][0]};
}

const WebElementField* findElementView(const WebViewOptions& v, const std::string& name) {
    for (const auto& f : v.elementFields)
        if (f.name == name) return &f;
    return nullptr;
}

void testTensorInvariants() {
    const MeshData mesh = makeMesh();
    const auto ids = allNodeIds(mesh);
    const std::size_t N = ids.size();
    // Node 0: sigma_x = 100, tau_xy = 50 (closed form: 50 +- 70.7107, 0). Node 1: hydrostatic 50 (repeated eigenvalues).
    // Node 2: random rotation of diag(7, 3, -1). The remaining nodes copy node 0.
    std::vector<std::vector<double>> rows(N, {100, 0, 0, 50, 0, 0});
    rows[1] = {50, 50, 50, 0, 0, 0};
    rows[2] = rotatedDiagonal(7, 3, -1, 0.7, 1, 2, 3);
    FemResults r;
    CHECK(parseCsv(tableText("node,S_xx,S_yy,S_zz,S_xy,S_yz,S_zx", ids, rows), r));
    WebViewOptions view;
    const auto b = bindResults(mesh, r, BindOptions{}, view);
    CHECK(b.success);
    const auto get = [&](const char* name, std::size_t n) {
        const auto* f = findView(view, name);
        CHECK(f != nullptr);
        return f ? f->values[n] : std::nan("");
    };
    const double root = std::sqrt(5000.0);
    CHECK(near(get("S von Mises", 0), std::sqrt(17500.0), 1e-12));
    CHECK(near(get("S principal 1", 0), 50 + root, 1e-12));
    CHECK(near(get("S principal 2", 0), 0.0, 1e-9));
    CHECK(near(get("S principal 3", 0), 50 - root, 1e-12));
    CHECK(near(get("S Tresca", 0), 2 * root, 1e-12));
    CHECK(near(get("S hydrostatic", 0), 100.0 / 3.0, 1e-12));
    CHECK(near(get("S von Mises", 1), 0.0, 1e-9) && near(get("S principal 1", 1), 50.0) && near(get("S principal 3", 1), 50.0));
    CHECK(near(get("S principal 1", 2), 7.0, 1e-12) && near(get("S principal 2", 2), 3.0, 1e-12) && near(get("S principal 3", 2), -1.0, 1e-12));
    // von Mises of the rotated state is invariant: sqrt(0.5 ((7-3)^2 + (3+1)^2 + (-1-7)^2)).
    CHECK(near(get("S von Mises", 2), std::sqrt(0.5 * (16 + 16 + 64)), 1e-12));

    // Strain: uniaxial eps = 1e-3 with Poisson 0.3 -> equivalent 2/3 (1 + nu) eps, volumetric (1 - 2 nu) eps.
    std::vector<std::vector<double>> strain(N, {1e-3, -3e-4, -3e-4, 0, 0, 0});
    strain[1] = {0, 0, 0, 1e-3, 0, 0};  // pure shear, tensor component 1e-3
    FemResults rs;
    CHECK(parseCsv(tableText("node,E_xx,E_yy,E_zz,E_xy,E_yz,E_zx", ids, strain), rs));
    WebViewOptions sv;
    CHECK(bindResults(mesh, rs, BindOptions{}, sv).success);
    auto sget = [&](const WebViewOptions& v, const char* name, std::size_t n) {
        const auto* f = findView(v, name);
        CHECK(f != nullptr);
        return f ? f->values[n] : std::nan("");
    };
    CHECK(near(sget(sv, "E equivalent", 0), 2.0 / 3.0 * 1.3 * 1e-3, 1e-12));
    CHECK(near(sget(sv, "E volumetric", 0), 0.4e-3, 1e-12));
    CHECK(near(sget(sv, "E principal 1", 0), 1e-3, 1e-12) && near(sget(sv, "E principal 3", 0), -3e-4, 1e-12));
    CHECK(near(sget(sv, "E principal 1", 1), 1e-3, 1e-12) && near(sget(sv, "E principal 3", 1), -1e-3, 1e-12));
    CHECK(near(sget(sv, "E max shear", 1), 2e-3, 1e-12));  // engineering gamma = 2 * tensor component
    CHECK(near(sget(sv, "E equivalent", 1), std::sqrt(2.0 / 3.0 * 2.0) * 1e-3, 1e-12));
    // Same file read as engineering shear strains: the tensor component is half as large.
    WebViewOptions ev;
    BindOptions eng;
    eng.engineeringShearStrain = true;
    CHECK(bindResults(mesh, rs, eng, ev).success);
    CHECK(near(sget(ev, "E principal 1", 1), 0.5e-3, 1e-12) && near(sget(ev, "E max shear", 1), 1e-3, 1e-12));
    // Stress is not affected by that option.
    WebViewOptions sv2;
    CHECK(bindResults(mesh, r, eng, sv2).success);
    CHECK(near(findView(sv2, "S von Mises")->values[0], std::sqrt(17500.0), 1e-12));

    // Missing component at a node -> the invariants are missing there and filled with the minimum.
    rows[3][4] = std::nan("");
    FemResults rm;
    std::string text = tableText("node,S_xx,S_yy,S_zz,S_xy,S_yz,S_zx", ids, rows);
    const auto cell = text.find("nan");
    if (cell != std::string::npos) text.replace(cell, 3, "");
    CHECK(parseCsv(text, rm));
    WebViewOptions mv;
    const auto mb = bindResults(mesh, rm, BindOptions{}, mv);
    CHECK(mb.success && mb.nodesWithoutData >= 1);
    double lowest = 1e300;
    for (const double v : findView(mv, "S von Mises")->values) lowest = std::min(lowest, v);
    CHECK(near(findView(mv, "S von Mises")->values[3], lowest));
    // Not a tensor: six unnamed columns do not get stress invariants.
    FemResults rn;
    CHECK(parseCsv(tableText("node,a,b,c,d,e,f", ids, std::vector<std::vector<double>>(N, {1, 2, 3, 4, 5, 6})), rn));
    WebViewOptions nv;
    CHECK(bindResults(mesh, rn, BindOptions{}, nv).success && nv.fields.size() == 6);
}

void testElementResults() {
    const MeshData mesh = makeMesh();
    const std::size_t E = mesh.getNumElements();
    std::vector<std::uint64_t> ids;
    std::vector<std::vector<double>> rows;
    for (std::uint64_t id = 1; id <= E; ++id) {
        if (id == 7) continue;  // element 7 has no record
        ids.push_back(id);
        rows.push_back({static_cast<double>(id), 100.0 * static_cast<double>(id), 0, 0, 0, 0, 0});
    }
    ids.push_back(9999);  // not in the mesh
    rows.push_back({0, 0, 0, 0, 0, 0, 0});
    FemResults r;
    const auto res = parseCsv(tableText("element,rank,sigma_xx,sigma_yy,sigma_zz,sigma_xy,sigma_yz,sigma_zx", ids, rows), r);
    CHECK(res);
    CHECK(r.fields.size() == 2 && r.fields[0].location == ResultLocation::Element && r.fields[1].location == ResultLocation::Element);

    WebViewOptions view;
    const auto b = bindResults(mesh, r, BindOptions{}, view);
    CHECK(b.success);
    CHECK(view.fields.empty() && !view.elementFields.empty() && b.elementFieldsBound == view.elementFields.size());
    CHECK(b.unmatchedElementRecords == 2 && b.elementsWithoutData == 1);  // id 9999 is counted once per field (2 fields)
    CHECK(hasCode(b.diagnostics, "BIND_UNMATCHED_ELEMENTS") && hasCode(b.diagnostics, "BIND_MISSING_ELEMENTS"));
    const auto* rank = findElementView(view, "rank");
    const auto* mises = findElementView(view, "sigma von Mises");
    CHECK(rank && mises);
    if (rank && mises) {
        CHECK(rank->values.size() == E);
        CHECK(near(rank->values[0], 1.0) && near(rank->values[47], 48.0));   // element id e is stored at index e - 1
        CHECK(near(rank->values[6], 1.0));                                   // missing element: field minimum
        CHECK(near(mises->values[9], 1000.0));                               // uniaxial: von Mises = |sigma_xx|
    }

    // Node and element ids in one table are refused.
    FemResults bad;
    const auto conflict = parseCsv("node,element,v\n1,1,2\n", bad);
    CHECK(!conflict && hasCode(conflict.diagnostics, "CSV_NODE_AND_ELEMENT"));

    // The writer paints each triangle with the value of the element that owns it. Independent check: with
    // value = element id, every triangle's three vertices must be nodes of that very element.
    std::vector<std::vector<double>> idRows;
    std::vector<std::uint64_t> allIds;
    for (std::uint64_t id = 1; id <= E; ++id) { allIds.push_back(id); idRows.push_back({static_cast<double>(id)}); }
    FemResults rid;
    CHECK(parseCsv(tableText("element,eid_value", allIds, idRows), rid));
    WebViewOptions iv;
    CHECK(bindResults(mesh, rid, BindOptions{}, iv).success);
    std::vector<unsigned char> image;
    const auto written = WebViewWriter::build(mesh, iv, image);
    CHECK(written.success);
    if (!written.success) return;
    const std::size_t jsonSize = le32(&image[12]);
    const std::string json(reinterpret_cast<const char*>(&image[24]), jsonSize);
    const unsigned char* bin = image.data() + 24 + jsonSize;
    CHECK(json.find("\"location\":\"element\"") != std::string::npos);
    auto numberAfter = [&](const std::string& key, std::size_t from = 0) {
        const auto k = json.find(key, from);
        CHECK(k != std::string::npos);
        return k == std::string::npos ? std::size_t(0) : static_cast<std::size_t>(std::stoul(json.substr(k + key.size())));
    };
    const std::size_t posOff = numberAfter("\"positions\":{\"offset\":"), triOff = numberAfter("\"triIndices\":{\"offset\":");
    const std::size_t fieldAt = json.find("\"location\":\"element\"");
    const std::size_t valOff = numberAfter("\"offset\":", fieldAt);
    double origin[3];
    {
        const auto k = json.find("\"origin\":[");
        std::sscanf(json.c_str() + k + 10, "%lf,%lf,%lf", &origin[0], &origin[1], &origin[2]);
    }
    // Element lookup: id -> its node coordinates.
    std::vector<std::vector<std::array<double, 3>>> corners(E + 1);
    for (const auto& block : mesh.getElementBlocks())
        for (std::size_t e = 0; e < block.size(); ++e)
            for (std::size_t k = 0; k < block.nodesPerElement; ++k)
                corners[block.ids[e]].push_back(mesh.nodeCoordinates(block.connectivity[e * block.nodesPerElement + k]));
    std::size_t checked = 0, wrong = 0;
    for (std::size_t t = 0; t < written.triangles; ++t) {
        float value;
        std::memcpy(&value, bin + valOff + 4 * t, 4);
        const std::size_t id = static_cast<std::size_t>(std::lround(value));
        if (id < 1 || id > E) { ++wrong; continue; }
        for (int v = 0; v < 3; ++v) {
            std::uint32_t vi;
            std::memcpy(&vi, bin + triOff + 4 * (3 * t + static_cast<std::size_t>(v)), 4);
            float p[3];
            std::memcpy(p, bin + posOff + 12 * vi, 12);
            bool found = false;
            for (const auto& c : corners[id])
                found = found || (std::fabs(c[0] - (p[0] + origin[0])) < 1e-4 && std::fabs(c[1] - (p[1] + origin[1])) < 1e-4 &&
                                  std::fabs(c[2] - (p[2] + origin[2])) < 1e-4);
            if (!found) ++wrong;
        }
        ++checked;
    }
    CHECK(checked == written.triangles && checked > 0);
    CHECK(wrong == 0);

    // Writer validation of element fields.
    WebViewOptions badView;
    badView.elementFields.push_back({"x", std::vector<double>(E - 1, 0.0)});
    CHECK(!WebViewWriter::build(mesh, badView, image).success);
    badView.elementFields[0].values.assign(E, 0.0);
    badView.elementFields[0].values[4] = std::nan("");
    CHECK(!WebViewWriter::build(mesh, badView, image).success);
}

/** Result file with `count` modes (ICTYPE 2) holding DISP = shape_k * (x, 0, 0) and a frequency per mode. */
std::string makeModalFile(const MeshData& mesh, const std::vector<double>& frequencies, bool withStatic = false) {
    FrdWriter w;
    w.format = 1;
    w.header();
    w.nodes(mesh, 0.0, 0);
    w.elementsStub();
    int number = 0;
    auto emit = [&](double value, int kind, double amplitude) {
        w.resultHeader(value, static_cast<long long>(mesh.getNumNodes()), kind, ++number);
        w.dataset("DISP", 4, 1);
        w.component("D1", 2, 1, 0, 0);
        w.component("D2", 2, 2, 0, 0);
        w.component("D3", 2, 3, 0, 0);
        w.component("ALL", 2, 0, 0, 1, "ALL");
        for (std::size_t n = 0; n < mesh.getNumNodes(); ++n) {
            const auto c = mesh.nodeCoordinates(n);
            w.record(static_cast<long long>(mesh.nodeId(n)), {e12(amplitude * (c[0] - 10.0)), e12(amplitude * 0.1), e12(0.0)});
        }
        w.end();
    };
    if (withStatic) emit(1.0, 0, 1.0);
    for (std::size_t k = 0; k < frequencies.size(); ++k) emit(frequencies[k], 2, 0.1 * static_cast<double>(k + 1));
    w.finish();
    return w.text;
}

void testModes() {
    const MeshData mesh = makeMesh();
    // Three modes, the last two with the same frequency (a degenerate pair must stay two modes).
    FemResults r;
    const auto res = parseFrd(makeModalFile(mesh, {12.5, 40.0, 40.0}), r);
    CHECK(res);
    CHECK(r.steps.size() == 3 && r.fields.size() == 3);
    CHECK(r.modeSteps().size() == 3);
    if (r.steps.size() == 3) {
        CHECK(r.steps[0].mode == 1 && r.steps[2].mode == 3);
        CHECK(r.steps[1].kind == StepKind::Frequency && near(r.steps[1].value, 40.0));
        CHECK(r.steps[0].label == "mode 1: f = 12.5");
    }

    // Auto: all steps are modes -> three modes, scalar fields from the first.
    WebViewOptions view;
    auto b = bindResults(mesh, r, BindOptions{}, view);
    CHECK(b.success && b.modesBound == 3 && view.modes.size() == 3 && view.displacement.empty());
    CHECK(b.step == 0 && b.hasDisplacement);
    CHECK(b.modeFrequencies.size() == 3 && near(b.modeFrequencies[0], 12.5) && near(b.modeFrequencies[2], 40.0));
    if (view.modes.size() == 3) {
        const auto c = mesh.nodeCoordinates(10);
        CHECK(near(view.modes[0].displacement[10][0], 0.1 * (c[0] - 10.0), 1e-5));
        CHECK(near(view.modes[2].displacement[10][0], 0.3 * (c[0] - 10.0), 1e-5));
    }
    CHECK(view.resultsLabel.find("3 modes") != std::string::npos);

    // Explicit N keeps the first N; 0 disables the modal view and takes the last step like before.
    WebViewOptions two;
    BindOptions limited;
    limited.maxModes = 2;
    b = bindResults(mesh, r, limited, two);
    CHECK(b.success && two.modes.size() == 2);
    WebViewOptions single;
    BindOptions off;
    off.maxModes = 0;
    b = bindResults(mesh, r, off, single);
    CHECK(b.success && single.modes.empty() && single.displacement.size() == mesh.getNumNodes() && b.step == 2);
    // An explicit step selects a single state, not the modal view.
    WebViewOptions chosen;
    BindOptions pick;
    pick.step = 1;
    b = bindResults(mesh, r, pick, chosen);
    CHECK(b.success && chosen.modes.empty() && b.step == 1 && !chosen.displacement.empty());

    // Mixed file: static step + modes. Auto does not guess (last step, single view); N > 0 asks for the modes.
    FemResults mixed;
    CHECK(parseFrd(makeModalFile(mesh, {5.0, 6.0}, true), mixed));
    CHECK(mixed.steps.size() == 3 && mixed.modeSteps().size() == 2);
    WebViewOptions autoView;
    b = bindResults(mesh, mixed, BindOptions{}, autoView);
    CHECK(b.success && autoView.modes.empty() && b.step == 2);
    WebViewOptions asked;
    BindOptions modal;
    modal.maxModes = 5;
    b = bindResults(mesh, mixed, modal, asked);
    CHECK(b.success && asked.modes.size() == 2 && b.step == 1);   // first mode is the reference step

    // Requesting modes from a results file that has none is an error, and leaves the view untouched.
    FemResults staticOnly;
    CHECK(parseFrd(makeResultFile(mesh), staticOnly));
    WebViewOptions untouched;
    const auto fail = bindResults(mesh, staticOnly, modal, untouched);
    CHECK(!fail.success && untouched.fields.empty() && untouched.modes.empty());

    // Writer: modes become per-mode chunks and replace the single displacement.
    std::vector<unsigned char> image;
    const auto written = WebViewWriter::build(mesh, view, image);
    CHECK(written.success);
    if (written.success) {
        const std::string json(reinterpret_cast<const char*>(&image[24]), le32(&image[12]));
        CHECK(json.find("\"modes\":[{\"label\":\"mode 1: f = 12.5\",\"frequency\":12.5") != std::string::npos);
        CHECK(json.find("\"displacement\":{\"offset\"") == std::string::npos);
        CHECK(json.find("\"deformation\":{") != std::string::npos);
    }
    WebViewOptions both = view;
    both.displacement.assign(mesh.getNumNodes(), {0, 0, 0});
    CHECK(!WebViewWriter::build(mesh, both, image).success);
    WebViewOptions wrongSize = view;
    wrongSize.modes[1].displacement.pop_back();
    CHECK(!WebViewWriter::build(mesh, wrongSize, image).success);
}


/* ------------------------------------------------------------------------- CalculiX .dat */

ResultImportResult parseDat(const std::string& text, FemResults& out) {
    std::istringstream in(text);
    return CalculixDatImporter{}.parse(in, out);
}

void testDatImporter() {
    // Layout of the CalculiX manual: header, blank line, rows, blank line. Element 1 has 2 integration points,
    // element 2 one; the stress order of this file is sxx syy szz sxy sxz syz.
    const std::string text =
        "\n displacements (vx,vy,vz) for set NALL and time  0.1000000E+01\n\n"
        "         1  0.100000E-01 -0.200000E-02  0.000000E+00\n"
        "         2  0.200000E-01  1.50000-101   0.300000E+00\n\n"
        " stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set EALL and time  0.1000000E+01\n\n"
        "         1   1  1.000000E+02  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00\n"
        "         1   2  3.000000E+02  0.000000E+00  0.000000E+00  1.000000E+02  0.000000E+00  0.000000E+00\n"
        "         2   1 -5.000000E+01  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00  0.000000E+00\n\n"
        " total force (fx,fy,fz) for set NALL and time  0.1000000E+01\n\n"
        "        0.100000E+01  0.200000E+00  0.300000E+00\n\n"
        " displacements (vx,vy,vz) for set NALL and time  0.2000000E+01\n\n"
        "         1  0.200000E-01 -0.400000E-02  0.000000E+00\n\n"
        "     E I G E N V A L U E   O U T P U T\n\n MODE NO    EIGENVALUE                       FREQUENCY\n"
        "                                     REAL PART            IMAGINARY PART\n"
        "                           (RAD/TIME)      (CYCLES/TIME     (RAD/TIME)\n\n"
        "      1  -0.5975806E+02   0.0000000E+00   0.0000000E+00   0.7730334E+01\n"
        "      2   0.1543140E+01   0.1242232E+01   0.1977073E+00   0.0000000E+00\n"
        "     10   0.8141709E+04   0.9023142E+02   0.1436078E+02   0.0000000E+00\n\n"
        " P A R T I C I P A T I O N   F A C T O R S\n\n      1   0.1E+00  0.2E+00  0.3E+00\n";
    FemResults r;
    const auto res = parseDat(text, r);
    CHECK(res);
    CHECK(r.format == "CalculiX DAT");
    CHECK(r.steps.size() == 2 && r.steps[0].kind == StepKind::Time && near(r.steps[1].value, 2.0));
    CHECK(r.fields.size() == 3);
    const auto* disp = findField(r, "DISP", 0);
    CHECK(disp && disp->location == ResultLocation::Node && disp->ids.size() == 2 && disp->components[0] == "VX");
    CHECK(disp && near(disp->values[0], 0.01) && near(disp->values[1], -0.002) && near(disp->values[4], 1.5e-101, 1e-9));  // "1.50000-101"
    const auto* stress = findField(r, "STRESS", 0);
    CHECK(stress && stress->location == ResultLocation::Element && stress->ids.size() == 2);
    if (stress && stress->ids.size() == 2) {
        CHECK(stress->components.size() == 6 && stress->components[4] == "SXZ" && stress->components[5] == "SYZ");
        CHECK(near(stress->values[0], 200.0) && near(stress->values[3], 50.0));    // mean of the two points of element 1
        CHECK(near(stress->values[6], -50.0));                                      // element 2 has one point
    }
    CHECK(hasCode(res.diagnostics, "DAT_IP_AVERAGED") && hasCode(res.diagnostics, "DAT_BLOCK_SKIPPED"));  // averaging announced, TOTALS skipped
    CHECK(r.eigenmodes.size() == 3);
    if (r.eigenmodes.size() == 3) {
        CHECK(r.eigenmodes[0].mode == 1 && r.eigenmodes[0].eigenvalue < 0 && near(r.eigenmodes[0].imaginary, 7.730334));
        CHECK(hasCode(res.diagnostics, "DAT_NEGATIVE_EIGENVALUE"));
        CHECK(r.eigenmodes[2].mode == 10 && near(r.eigenmodes[2].frequency, 14.36078) && near(r.eigenmodes[2].omega, 90.23142));
        // columns are consistent: omega^2 = eigenvalue, f = omega / 2 pi
        CHECK(near(r.eigenmodes[1].omega * r.eigenmodes[1].omega, r.eigenmodes[1].eigenvalue, 1e-5));
        CHECK(near(r.eigenmodes[1].omega / (2 * 3.14159265358979), r.eigenmodes[1].frequency, 1e-5));
    }
    // the participation-factor table that follows is not read as eigenvalues
    CHECK(r.eigenmodes.size() == 3);

    // Two sets of the same quantity in one increment form one field.
    FemResults two;
    CHECK(parseDat(" displacements (vx,vy,vz) for set A and time 1.0E+00\n\n 1 1.0E+00 0.0E+00 0.0E+00\n\n"
                   " displacements (vx,vy,vz) for set B and time 1.0E+00\n\n 2 2.0E+00 0.0E+00 0.0E+00\n 1 9.0E+00 0.0E+00 0.0E+00\n\n", two));
    CHECK(two.fields.size() == 1 && two.fields[0].ids.size() == 2 && near(two.fields[0].values[0], 1.0));  // node 1 of the first set wins

    // Errors with line numbers.
    FemResults bad;
    auto e1 = parseDat(" displacements (vx,vy,vz) for set A and time 1.0E+00\n\n 1 1.0E+00 0.0E+00\n", bad);
    CHECK(!e1 && hasCode(e1.diagnostics, "DAT_ROW_WIDTH") && e1.diagnostics.back().line == 3);
    auto e2 = parseDat(" displacements (vx,vy,vz) for set A and time 1.0E+00\n\n 1 1.0E+00 abc 0.0E+00\n", bad);
    CHECK(!e2 && hasCode(e2.diagnostics, "DAT_BAD_VALUE"));
    auto e3 = parseDat(" displacements (vx,vy,vz) for set A and time 1.0E+00\n\n 1 1 1 1\n 1 1 1 1\n", bad);
    CHECK(!e3 && hasCode(e3.diagnostics, "DAT_DUPLICATE_NODE"));
    auto e4 = parseDat("nothing useful here\n", bad);
    CHECK(!e4 && hasCode(e4.diagnostics, "DAT_NO_RESULTS"));
    CHECK(r.fields.size() == 3);   // a failed parse leaves other results untouched

    // Extension dispatch.
    CHECK(CalculixDatImporter{}.canRead("run.DAT") && !CalculixDatImporter{}.canRead("run.frd"));
}

void testDatBindingAndMerge() {
    const MeshData mesh = makeMesh();
    // .frd of the run (nodal, times 0.5 and 1.0 as written by makeResultFile) + .dat with element stress at the same times.
    FemResults frd;
    CHECK(parseFrd(makeResultFile(mesh), frd));
    std::ostringstream dat;
    dat.precision(7);
    for (int step = 1; step <= 2; ++step) {
        char t[40];
        std::snprintf(t, sizeof t, "%13.7E", 0.5 * step);
        dat << "\n stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set EALL and time " << t << "\n\n";
        for (std::uint64_t id = 1; id <= mesh.getNumElements(); ++id) {
            // two integration points: the mean is sxx = 100 id, sxy = 40 (a state with a non-zero principal rotation)
            dat << "  " << id << " 1 " << (100.0 * id - 10) * step << " 0 0 40 0 0\n";
            dat << "  " << id << " 2 " << (100.0 * id + 10) * step << " 0 0 40 0 0\n";
        }
        dat << "\n";
    }
    FemResults extra;
    CHECK(parseDat(dat.str(), extra));
    // frd times are 0.5 / 1.0 printed with 6 digits, dat times with 7: they still match.
    const auto merged = mergeResults(frd, extra);
    CHECK(merged.status == ImportStatus::Success);
    CHECK(frd.steps.size() == 2 && frd.format == "CalculiX FRD + CalculiX DAT");
    CHECK(findField(frd, "DISP", 1) && !frd.fields.empty());
    std::size_t elementFields = 0;
    for (const auto& f : frd.fields) elementFields += f.location == ResultLocation::Element ? 1 : 0;
    CHECK(elementFields == 2);

    WebViewOptions view;
    const auto b = bindResults(mesh, frd, BindOptions{}, view);
    CHECK(b.success && b.step == 1 && b.elementFieldsBound == 12 && b.hasDisplacement);
    const auto* mises = findElementView(view, "STRESS von Mises");
    const auto* p1 = findElementView(view, "STRESS principal 1");
    const auto* p3 = findElementView(view, "STRESS principal 3");
    CHECK(mises && p1 && p3);
    if (mises && p1 && p3) {
        // element 5, step 2 (factor 2): sxx = 2 * 500 = 1000, sxy = 40 (the file does not scale it) -> closed form
        const double sxx = 1000.0, sxy = 40.0, c = std::sqrt(sxx * sxx / 4 + sxy * sxy);
        CHECK(near(p1->values[4], sxx / 2 + c, 1e-12) && near(p3->values[4], sxx / 2 - c, 1e-12));
        CHECK(near(mises->values[4], std::sqrt(sxx * sxx + 3 * sxy * sxy), 1e-12));
    }
    // The SXZ/SYZ order of the .dat does not matter: the same state with the shear in sxz keeps the same invariants.
    FemResults swapped;
    CHECK(parseDat(" stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set E and time 1.0E+00\n\n 1 1 0 0 0 0 40 0\n\n", swapped));
    WebViewOptions sw;
    BindOptions pick;
    pick.step = 0;
    CHECK(bindResults(mesh, swapped, pick, sw).success);
    CHECK(near(findElementView(sw, "STRESS von Mises")->values[0], std::sqrt(3.0) * 40.0, 1e-12));

    // A step without partner is added with a warning; an already present field is kept.
    FemResults later;
    CHECK(parseDat(" stresses (elem, integ.pnt.,sxx,syy,szz,sxy,sxz,syz) for set E and time 7.0E+00\n\n 1 1 1 0 0 0 0 0\n\n", later));
    const auto m2 = mergeResults(frd, later);
    CHECK(hasCode(m2.diagnostics, "MERGE_NEW_STEP") && frd.steps.size() == 3);
    const auto m3 = mergeResults(frd, extra);
    CHECK(hasCode(m3.diagnostics, "MERGE_FIELD_PRESENT"));

    // readResultsMerged: files on disk, one unreadable file leaves the destination untouched.
    FemResults untouched;
    untouched.format = "keep";
    const auto bad = readResultsMerged({"/nonexistent/a.frd"}, untouched);
    CHECK(!bad && untouched.format == "keep");
    CHECK(!readResultsMerged({}, untouched));
}

} // namespace

int main() {
    testFrdParsing();
    testFrdNumbersAndContinuation();
    testFrdRealWorldLines();
    testFrdErrors();
    testCsv();
    testBinding();
    testBindingPartialData();
    testWebViewIntegration();
    testTensorInvariants();
    testElementResults();
    testModes();
    testDatImporter();
    testDatBindingAndMerge();
    if (g_failures) {
        std::cerr << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "fem_results_test: all checks passed\n";
    return 0;
}
