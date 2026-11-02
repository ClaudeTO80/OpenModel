#include <OpenModel/Fatigue/FatigueReport.hpp>

#include <cassert>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
ResultField stressField(std::size_t step, std::uint64_t id, double xx) {
    ResultField f;
    f.name = "STRESS";
    f.components = {"SXX", "SYY", "SZZ", "SXY", "SXZ", "SYZ"};
    f.step = step;
    f.location = OpenModel::ResultLocation::Element;
    f.ids = {id};
    f.values = {xx, 0.0, 0.0, 0.0, 0.0, 0.0};
    f.semantics.quantity = ResultQuantityKind::Stress;
    f.semantics.components = ResultComponentKind::SymmetricTensor;
    f.semantics.frame = ResultCoordinateFrame::Global;
    f.semantics.unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    f.semantics.unit.symbol = "MPa";
    f.semantics.unit.scaleToSI = 1.0e6;
    return f;
}

FatigueMaterial makeMaterial() {
    PhysicalUnit unit;
    unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    unit.symbol = "MPa";
    unit.scaleToSI = 1.0e6;
    FatigueCurve curve;
    std::string diagnostic;
    assert(FatigueCurve::create(FatigueCurveKind::StressLifeSN, unit,
        {{293.15, {{25.0, 1.0e7}, {50.0, 1.0e6}, {100.0, 1.0e5}}}},
        curve, diagnostic));
    FatigueMaterial material;
    material.name = "Report test material";
    material.materialId = "REPORT-01";
    material.curves.push_back(curve);
    return material;
}
}

int main() {
    FemResults results;
    const double history[] = {0.0, 100.0, 0.0, -100.0, 0.0};
    for (std::size_t step = 0; step < 5; ++step) {
        results.steps.push_back({"time", static_cast<double>(step), StepKind::Time, 0});
        results.fields.push_back(stressField(step, 42, history[step]));
        results.fields.push_back(stressField(step, 43, history[step] * 0.5));
    }
    ResultSourceMetadata metadata;
    metadata.solverName = "ReportFixture";
    OpenModelFemResultsSource source(results, metadata);
    auto material = makeMaterial();

    StressProjection projection;
    projection.direction = {{1.0, 0.0, 0.0}};
    FatiguePositionInput p1;
    p1.position.location = Fatigue::ResultLocation::Element;
    p1.position.entityId = 42;
    p1.material = &material;
    p1.projection = projection;
    p1.label = "Element 42";

    FatiguePositionInput p2 = p1;
    p2.position.entityId = 43;
    p2.label = "Element 43";

    FatiguePositionInput missingMaterial = p1;
    missingMaterial.position.entityId = 99;
    missingMaterial.material = nullptr;
    missingMaterial.label = "Unassigned";

    const auto report = FatigueReportBuilder::build(
        source, "STRESS", {p1, p2}, 293.15);
    assert(report);
    assert(report.positions.size() == 2);
    assert(report.successfulPositions == 2);
    assert(report.failedPositions == 0);
    assert(report.positiveDamagePositions == 2);
    assert(report.hasMaximumDamage);
    assert(report.maximumDamageIndex == 0);
    assert(report.positions[0].hasDamage);
    assert(report.positions[1].damage > 0.0);
    assert(report.positions[0].analysis.source.solverName == "ReportFixture");

    const auto partial = FatigueReportBuilder::build(
        source, "STRESS", {p1, missingMaterial, p2}, 293.15);
    assert(partial.status == FatigueReportStatus::PartialFailure);
    assert(partial.successfulPositions == 2);
    assert(partial.failedPositions == 1);
    assert(!partial.positions[1].hasDamage);
    assert(partial.positions[1].analysis.diagnostic.find("No material") != std::string::npos);

    const auto allFailed = FatigueReportBuilder::build(
        source, "STRESS", {missingMaterial}, 293.15);
    assert(allFailed.status == FatigueReportStatus::AllPositionsFailed);
    assert(!allFailed.positions.front().hasDamage);

    assert(FatigueReportBuilder::build(source, "STRESS", {}, 293.15).status ==
           FatigueReportStatus::EmptySelection);
    assert(FatigueReportBuilder::build(source, "", {p1}, 293.15).status ==
           FatigueReportStatus::InvalidInput);
    return 0;
}
