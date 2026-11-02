#include <OpenModel/Fatigue/FatigueAnalysis.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
ResultField stressField(std::size_t step, double xx) {
    ResultField f;
    f.name = "STRESS";
    f.components = {"SXX", "SYY", "SZZ", "SXY", "SXZ", "SYZ"};
    f.step = step;
    f.location = OpenModel::ResultLocation::Element;
    f.ids = {42};
    f.values = {xx, 0.0, 0.0, 0.0, 0.0, 0.0};
    f.semantics.quantity = ResultQuantityKind::Stress;
    f.semantics.components = ResultComponentKind::SymmetricTensor;
    f.semantics.frame = ResultCoordinateFrame::Global;
    f.semantics.unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    f.semantics.unit.symbol = "MPa";
    f.semantics.unit.scaleToSI = 1.0e6;
    return f;
}

FemResults makeResults(const std::vector<double>& values) {
    FemResults results;
    for (std::size_t i = 0; i < values.size(); ++i) {
        results.steps.push_back({"time", static_cast<double>(i), StepKind::Time, 0});
        results.fields.push_back(stressField(i, values[i]));
    }
    return results;
}
}

int main() {
    auto results = makeResults({0.0, 100.0, 0.0, -100.0, 0.0});
    ResultSourceMetadata metadata;
    metadata.solverName = "SyntheticTestSolver";
    metadata.resultFormat = "test-fixture";
    OpenModelFemResultsSource source(results, metadata);

    PhysicalUnit stressUnit;
    stressUnit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    stressUnit.symbol = "MPa";
    stressUnit.scaleToSI = 1.0e6;

    FatigueCurve sn;
    std::string diagnostic;
    assert(FatigueCurve::create(FatigueCurveKind::StressLifeSN, stressUnit,
        {{293.15, {{25.0, 1.0e7}, {50.0, 1.0e6}, {100.0, 1.0e5}}}},
        sn, diagnostic));
    FatigueMaterial material;
    material.name = "Synthetic steel";
    material.materialId = "SYN-01";
    material.source = "Deterministic unit test";
    material.curves.push_back(sn);

    ResultPosition position;
    position.location = Fatigue::ResultLocation::Element;
    position.entityId = 42;
    StressProjection projection;
    projection.direction = {{1.0, 0.0, 0.0}};

    const auto report = FatigueAnalysis::run(
        source, "STRESS", position, material, projection, 293.15);
    assert(report);
    assert(report.status == FatigueAnalysisStatus::Success);
    assert(report.history.size() == 5);
    assert(report.cycleCounting.success);
    assert(!report.cycleCounting.cycles.empty());
    assert(report.damage);
    assert(report.damage.damage > 0.0);
    assert(report.fieldName == "STRESS");
    assert(report.position.entityId == 42);
    assert(report.source.solverName == "SyntheticTestSolver");
    assert(report.history.samples().front().prepared.raw.position.entityId == 42);

    auto noSN = material;
    noSN.curves.clear();
    assert(FatigueAnalysis::run(source, "STRESS", position, noSN, projection, 293.15).status ==
           FatigueAnalysisStatus::InvalidMaterial);

    auto wrongProjection = projection;
    wrongProjection.direction = {{2.0, 0.0, 0.0}};
    assert(FatigueAnalysis::run(source, "STRESS", position, material, wrongProjection, 293.15).status ==
           FatigueAnalysisStatus::CycleCountingFailed);

    assert(FatigueAnalysis::run(source, "STRESS", position, material, projection, 250.0).status ==
           FatigueAnalysisStatus::DamageEvaluationFailed);

    // Two separate blocks deliberately restart their local time at zero.
    auto secondResults = makeResults({0.0, 50.0, 0.0, -50.0, 0.0});
    OpenModelFemResultsSource secondSource(secondResults);
    FatigueLoadBlockInput first;
    first.label = "High-amplitude block";
    first.source = &source;
    first.fieldName = "STRESS";
    first.position = position;
    first.projection = projection;
    first.repetitions = 2.0;

    FatigueLoadBlockInput second = first;
    second.label = "Low-amplitude block";
    second.source = &secondSource;
    second.repetitions = 3.0;

    const auto spectrum = FatigueSpectrumAnalysis::run({first, second}, material, 293.15);
    assert(spectrum);
    assert(spectrum.blocks.size() == 2);
    assert(spectrum.blocks[0].analysis.history.samples().front().time == 0.0);
    assert(spectrum.blocks[1].analysis.history.samples().front().time == 0.0);
    const double expected = 2.0 * spectrum.blocks[0].damagePerBlock +
                            3.0 * spectrum.blocks[1].damagePerBlock;
    assert(std::fabs(spectrum.totalDamage - expected) < 1e-14);
    assert(std::fabs(spectrum.spectrumLife - 1.0 / expected) < 1e-8);
    assert(spectrum.blocks[0].label == "High-amplitude block");
    assert(spectrum.blocks[1].label == "Low-amplitude block");

    assert(FatigueSpectrumAnalysis::run({}, material, 293.15).status ==
           FatigueSpectrumStatus::EmptySpectrum);
    auto invalidBlock = first;
    invalidBlock.repetitions = 0.0;
    assert(FatigueSpectrumAnalysis::run({invalidBlock}, material, 293.15).status ==
           FatigueSpectrumStatus::InvalidBlock);
    auto failingBlock = first;
    failingBlock.fieldName = "MISSING";
    const auto failed = FatigueSpectrumAnalysis::run({first, failingBlock}, material, 293.15);
    assert(failed.status == FatigueSpectrumStatus::BlockAnalysisFailed);
    assert(failed.failedBlockIndex == 1);
    assert(failed.blocks.size() == 2);
    assert(failed.blocks.front().analysis);
    assert(!failed.blocks.back().analysis);
    return 0;
}
