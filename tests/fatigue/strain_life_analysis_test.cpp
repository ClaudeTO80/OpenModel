#include <OpenModel/Fatigue/StrainLifeAnalysis.hpp>

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
    OpenModelFemResultsSource source(results);

    PhysicalUnit stress;
    stress.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    stress.symbol = "MPa";
    stress.scaleToSI = 1.0e6;
    PhysicalUnit strain;
    strain.dimension = {true, 0, 0, 0, 0, 0, 0, 0};
    strain.symbol = "1";
    strain.scaleToSI = 1.0;

    CyclicStressStrainParameters cyclic;
    cyclic.stressUnit = stress;
    cyclic.strainUnit = strain;
    cyclic.youngsModulus = 210000.0;
    cyclic.cyclicStrengthCoefficient = 1000.0;
    cyclic.cyclicStrainHardeningExponent = 0.15;

    StrainLifeMaterialParameters strainLife;
    strainLife.stressUnit = stress;
    strainLife.strainUnit = strain;
    strainLife.youngsModulus = cyclic.youngsModulus;
    strainLife.fatigueStrengthCoefficient = 1000.0;
    strainLife.fatigueStrengthExponent = -0.1;
    strainLife.fatigueDuctilityCoefficient = 0.5;
    strainLife.fatigueDuctilityExponent = -0.5;

    ResultPosition position;
    position.location = Fatigue::ResultLocation::Element;
    position.entityId = 42;
    StressProjection projection;
    projection.direction = {{1.0, 0.0, 0.0}};

    const auto result = StrainLifeAnalysis::run(
        source, "STRESS", position, projection, cyclic, strainLife);
    assert(result);
    assert(result.status == StrainLifeAnalysisStatus::Success);
    assert(result.history.size() == 5);
    assert(result.cycleCounting.success);
    assert(!result.cycleCounting.cycles.empty());
    assert(result.localCorrection);
    assert(result.localCorrection.cycles.size() == result.cycleCounting.cycles.size());
    assert(result.damage && result.damage.damage > 0.0);
    for (std::size_t i = 0; i < result.cycleCounting.cycles.size(); ++i) {
        assert(result.localCorrection.cycles[i].startSampleIndex ==
               result.cycleCounting.cycles[i].start.historySampleIndex);
        assert(result.localCorrection.cycles[i].endSampleIndex ==
               result.cycleCounting.cycles[i].end.historySampleIndex);
    }

    const auto morrow = StrainLifeAnalysis::run(
        source, "STRESS", position, projection, cyclic, strainLife,
        StrainLifeMeanStressModel::Morrow);
    assert(morrow && morrow.damage.damage > 0.0);

    const auto swt = StrainLifeAnalysis::run(
        source, "STRESS", position, projection, cyclic, strainLife,
        StrainLifeMeanStressModel::SmithWatsonTopper);
    assert(swt && swt.damage.damage > 0.0);

    auto wrongUnit = cyclic;
    wrongUnit.stressUnit.symbol = "Pa";
    assert(StrainLifeAnalysis::run(source, "STRESS", position, projection,
        wrongUnit, strainLife).status == StrainLifeAnalysisStatus::InvalidInput);

    auto wrongModulus = strainLife;
    wrongModulus.youngsModulus *= 1.01;
    assert(StrainLifeAnalysis::run(source, "STRESS", position, projection,
        cyclic, wrongModulus).status == StrainLifeAnalysisStatus::InvalidInput);

    auto invalidProjection = projection;
    invalidProjection.direction = {{0.0, 0.0, 0.0}};
    assert(StrainLifeAnalysis::run(source, "STRESS", position, invalidProjection,
        cyclic, strainLife).status == StrainLifeAnalysisStatus::CycleCountingFailed);

    assert(StrainLifeAnalysis::run(source, "MISSING", position, projection,
        cyclic, strainLife).status == StrainLifeAnalysisStatus::HistoryBuildFailed);
    return 0;
}
