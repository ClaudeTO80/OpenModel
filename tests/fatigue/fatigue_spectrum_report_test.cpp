#include <OpenModel/Fatigue/FatigueSpectrumReport.hpp>

#include <cassert>
#include <sstream>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
ResultField makeField(std::size_t step, double a, double b) {
    ResultField field;
    field.name = "STRESS";
    field.components = {"SXX", "SYY", "SZZ", "SXY", "SXZ", "SYZ"};
    field.step = step;
    field.location = OpenModel::ResultLocation::Element;
    field.ids = {10, 20};
    field.values = {a, 0, 0, 0, 0, 0, b, 0, 0, 0, 0, 0};
    field.semantics.quantity = ResultQuantityKind::Stress;
    field.semantics.components = ResultComponentKind::SymmetricTensor;
    field.semantics.frame = ResultCoordinateFrame::Global;
    field.semantics.unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    field.semantics.unit.symbol = "MPa";
    field.semantics.unit.scaleToSI = 1.0e6;
    return field;
}
FatigueMaterial makeMaterial() {
    PhysicalUnit unit;
    unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    unit.symbol = "MPa";
    unit.scaleToSI = 1.0e6;
    FatigueCurve curve;
    std::string diagnostic;
    assert(FatigueCurve::create(FatigueCurveKind::StressLifeSN, unit,
        {{293.15, {{25, 1e7}, {50, 1e6}, {100, 1e5}, {200, 1e4}}}},
        curve, diagnostic));
    FatigueMaterial material;
    material.name = "Spectrum report fixture";
    material.materialId = "SPECTRUM-01";
    material.curves.push_back(curve);
    return material;
}
}

int main() {
    FemResults results;
    const double history[] = {0, 100, 0, -100, 0};
    for (std::size_t step = 0; step < 5; ++step) {
        results.steps.push_back({"time", static_cast<double>(step), StepKind::Time, 0});
        results.fields.push_back(makeField(step, history[step], history[step] * 0.5));
    }
    ResultSourceMetadata metadata;
    metadata.solverName = "SpectrumReportFixture";
    OpenModelFemResultsSource source(results, metadata);
    auto material = makeMaterial();

    FatigueLoadBlockInput block;
    block.label = "operating block";
    block.source = &source;
    block.fieldName = "STRESS";
    block.projection.direction = {{1, 0, 0}};
    block.repetitions = 2.0;

    FatigueSpectrumPositionInput p1;
    p1.position.location = Fatigue::ResultLocation::Element;
    p1.position.entityId = 10;
    p1.material = &material;
    p1.label = "Element 10";
    p1.blocks = {block};

    FatigueSpectrumPositionInput p2 = p1;
    p2.position.entityId = 20;
    p2.label = "Element 20";

    FatigueSpectrumPositionInput unassigned = p1;
    unassigned.position.entityId = 30;
    unassigned.material = nullptr;

    const auto report = FatigueSpectrumReportBuilder::build({p1, p2}, 293.15);
    assert(report);
    assert(report.positions.size() == 2);
    assert(report.successfulPositions == 2);
    assert(report.failedPositions == 0);
    assert(report.hasMaximumDamage);
    assert(report.maximumDamageIndex == 0);
    assert(report.positions[0].hasDamage);
    assert(report.positions[1].hasDamage);
    assert(report.positions[0].damage > report.positions[1].damage);
    assert(report.positions[0].spectrum.blocks.size() == 1);
    assert(report.positions[0].spectrum.blocks[0].repetitions == 2.0);

    std::ostringstream summary, blockCsv, json;
    assert(FatigueReportWriter::writeCsvSpectrumSummary(report, summary));
    assert(FatigueReportWriter::writeCsvSpectrumBlocks(report, blockCsv));
    assert(FatigueReportWriter::writeSpectrumJson(report, json, true));
    assert(summary.str().find("spectrum_life") != std::string::npos);
    assert(blockCsv.str().find("damage_contribution") != std::string::npos);
    assert(json.str().find("openmodel.fatigue-spectrum-report.v1") != std::string::npos);
    assert(json.str().find("\"cycles\":[") != std::string::npos);

    const auto partial = FatigueSpectrumReportBuilder::build(
        {p1, unassigned}, 293.15);
    assert(partial.status == FatigueSpectrumReportStatus::PartialFailure);
    assert(partial.successfulPositions == 1);
    assert(partial.failedPositions == 1);
    assert(!partial.positions[1].hasDamage);
    assert(partial.positions[1].spectrum.diagnostic.find("No fatigue material") != std::string::npos);

    const auto allFailed = FatigueSpectrumReportBuilder::build({unassigned}, 293.15);
    assert(allFailed.status == FatigueSpectrumReportStatus::AllPositionsFailed);
    assert(FatigueSpectrumReportBuilder::build({}, 293.15).status ==
           FatigueSpectrumReportStatus::EmptySelection);
    return 0;
}
