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
}

int main() {
    FemResults results;
    const double values[] = {0.0, 100.0, 0.0, -100.0, 0.0};
    for (std::size_t i = 0; i < 5; ++i) {
        results.steps.push_back({"time", static_cast<double>(i), StepKind::Time, 0});
        results.fields.push_back(stressField(i, values[i]));
    }
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
    return 0;
}
