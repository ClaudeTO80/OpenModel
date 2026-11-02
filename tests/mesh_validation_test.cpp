#include <OpenModel/MeshValidator.hpp>

#include <cassert>
#include <cmath>
#include <limits>

static bool hasCode(const MeshValidationReport& report, ValidationCode code) {
    for (const auto& issue : report.issues) if (issue.code == code) return true;
    return false;
}

int main() {
    // Positive-orientation unit tetrahedron: determinant is positive.
    MeshData good;
    auto a = good.addNode(10, 0.0, 0.0, 0.0);
    auto b = good.addNode(20, 1.0, 0.0, 0.0);
    auto c = good.addNode(30, 0.0, 1.0, 0.0);
    auto d = good.addNode(40, 0.0, 0.0, 1.0);
    good.addElement("volume", ElementType::Tetrahedron4, 100, {a, b, c, d});
    auto goodReport = MeshValidator::validate(good);
    assert(goodReport.isValid());
    assert(goodReport.checkedNodes == 4);
    assert(goodReport.checkedElements == 1);
    assert(goodReport.degenerateElements == 0);
    assert(goodReport.minimumTetrahedronQuality > 0.0);
    assert(goodReport.minimumTetrahedronQuality <= 1.0);

    // A regular tetrahedron has mean-ratio quality equal to one (within roundoff).
    MeshData regular;
    const double root3 = std::sqrt(3.0);
    const auto q0 = regular.addNode(1, 0.0, 0.0, 0.0);
    const auto q1 = regular.addNode(2, 1.0, 0.0, 0.0);
    const auto q2 = regular.addNode(3, 0.5, root3 / 2.0, 0.0);
    const auto q3 = regular.addNode(4, 0.5, root3 / 6.0, std::sqrt(2.0 / 3.0));
    regular.addElement("volume", ElementType::Tetrahedron4, 101, {q0, q1, q2, q3});
    const auto regularReport = MeshValidator::validate(regular);
    assert(regularReport.isValid());
    assert(std::abs(regularReport.minimumTetrahedronQuality - 1.0) < 1.0e-12);

    // Reversed node order is reported as negative orientation.
    MeshData reversed;
    a = reversed.addNode(1, 0.0, 0.0, 0.0);
    b = reversed.addNode(2, 0.0, 1.0, 0.0);
    c = reversed.addNode(3, 1.0, 0.0, 0.0);
    d = reversed.addNode(4, 0.0, 0.0, 1.0);
    reversed.addElement("volume", ElementType::Tetrahedron4, 7, {a, b, c, d});
    auto reversedReport = MeshValidator::validate(reversed);
    assert(reversedReport.hasErrors());
    assert(reversedReport.negativelyOrientedTetrahedra == 1);
    assert(hasCode(reversedReport, ValidationCode::NegativeOrientation));

    // Collinear triangle has zero area.
    MeshData flat;
    a = flat.addNode(1, 0.0, 0.0, 0.0);
    b = flat.addNode(2, 1.0, 0.0, 0.0);
    c = flat.addNode(3, 2.0, 0.0, 0.0);
    flat.addElement("surface", ElementType::Triangle3, 8, {a, b, c});
    auto flatReport = MeshValidator::validate(flat);
    assert(flatReport.hasErrors());
    assert(flatReport.degenerateElements == 1);
    assert(hasCode(flatReport, ValidationCode::DegenerateElement));

    // A quadrilateral with all four vertices collinear is degenerate.
    MeshData flatQuad;
    const auto f0 = flatQuad.addNode(1, 0.0, 0.0, 0.0);
    const auto f1 = flatQuad.addNode(2, 1.0, 0.0, 0.0);
    const auto f2 = flatQuad.addNode(3, 2.0, 0.0, 0.0);
    const auto f3 = flatQuad.addNode(4, 3.0, 0.0, 0.0);
    flatQuad.addElement("surface", ElementType::Quadrilateral4, 14, {f0, f1, f2, f3});
    const auto flatQuadReport = MeshValidator::validate(flatQuad);
    assert(flatQuadReport.hasErrors());
    assert(hasCode(flatQuadReport, ValidationCode::DegenerateElement));

    // Non-finite coordinates are caught before geometric calculations.
    MeshData nonFinite;
    a = nonFinite.addNode(1, 0.0, 0.0, 0.0);
    b = nonFinite.addNode(2, 1.0, 0.0, 0.0);
    c = nonFinite.addNode(3, 0.0, 1.0, 0.0);
    d = nonFinite.addNode(4, 0.0, 0.0, std::numeric_limits<double>::quiet_NaN());
    nonFinite.addElement("volume", ElementType::Tetrahedron4, 9, {a, b, c, d});
    auto nonFiniteReport = MeshValidator::validate(nonFinite);
    assert(nonFiniteReport.hasErrors());
    assert(hasCode(nonFiniteReport, ValidationCode::NonFiniteCoordinate));

    // Duplicate element IDs are caught even when they occur in different blocks.
    MeshData duplicate;
    a = duplicate.addNode(1, 0.0, 0.0, 0.0);
    b = duplicate.addNode(2, 1.0, 0.0, 0.0);
    c = duplicate.addNode(3, 0.0, 1.0, 0.0);
    d = duplicate.addNode(4, 0.0, 0.0, 1.0);
    duplicate.addElement("volume", ElementType::Tetrahedron4, 55, {a, b, c, d});
    duplicate.addElement("surface", ElementType::Triangle3, 55, {a, b, c});
    auto duplicateReport = MeshValidator::validate(duplicate);
    assert(hasCode(duplicateReport, ValidationCode::DuplicateElementId));

    // Triangle quality reports a scale-independent minimum angle.
    MeshData skinny;
    const auto s0 = skinny.addNode(1, 0.0, 0.0, 0.0);
    const auto s1 = skinny.addNode(2, 10.0, 0.0, 0.0);
    const auto s2 = skinny.addNode(3, 0.1, 0.01, 0.0);
    skinny.addElement("surface", ElementType::Triangle3, 11, {s0, s1, s2});
    MeshValidationOptions angleOptions;
    angleOptions.reportUnusedNodes = false;
    const auto skinnyReport = MeshValidator::validate(skinny, angleOptions);
    assert(skinnyReport.isValid());
    assert(skinnyReport.minimumTriangleAngleDegrees > 0.0);
    assert(skinnyReport.minimumTriangleAngleDegrees < 5.0);

    // Repeated corner nodes are diagnosed even if the mesh is structurally well formed.
    MeshData repeated;
    const auto r0 = repeated.addNode(1, 0.0, 0.0, 0.0);
    const auto r1 = repeated.addNode(2, 1.0, 0.0, 0.0);
    const auto r2 = repeated.addNode(3, 0.0, 1.0, 0.0);
    repeated.addElement("surface", ElementType::Triangle3, 12, {r0, r1, r1});
    const auto repeatedReport = MeshValidator::validate(repeated);
    assert(repeatedReport.hasErrors());
    assert(repeatedReport.duplicateNodeElements == 1);
    assert(hasCode(repeatedReport, ValidationCode::DuplicateNodeInElement));

    // Unreferenced nodes are reported as warnings, not fatal errors.
    MeshData unused;
    const auto u0 = unused.addNode(1, 0.0, 0.0, 0.0);
    const auto u1 = unused.addNode(2, 1.0, 0.0, 0.0);
    const auto u2 = unused.addNode(3, 0.0, 1.0, 0.0);
    unused.addNode(4, 9.0, 9.0, 9.0);
    unused.addElement("surface", ElementType::Triangle3, 13, {u0, u1, u2});
    const auto unusedReport = MeshValidator::validate(unused);
    assert(unusedReport.isValid());
    assert(unusedReport.unusedNodes == 1);
    assert(hasCode(unusedReport, ValidationCode::UnusedNode));

    // Invalid thresholds are rejected instead of silently changing validation semantics.
    MeshValidationOptions invalidOptions;
    invalidOptions.minimumTetrahedronQuality = 1.5;
    const auto invalidOptionsReport = MeshValidator::validate(good, invalidOptions);
    assert(invalidOptionsReport.hasErrors());
    assert(hasCode(invalidOptionsReport, ValidationCode::InvalidOptions));

    // Negative orientation can be downgraded to a warning for diagnostic workflows.
    MeshValidationOptions permissive;
    permissive.requirePositiveTetrahedronOrientation = false;
    assert(MeshValidator::validate(reversed, permissive).isValid());
}
