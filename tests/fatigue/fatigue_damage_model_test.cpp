#include <OpenModel/Fatigue/FatigueDamageModel.hpp>

#include <cassert>
#include <cmath>
#include <limits>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    PhysicalUnit stressUnit;
    stressUnit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    stressUnit.symbol = "MPa";
    stressUnit.scaleToSI = 1.0e6;

    FatigueCurve curve;
    std::string diagnostic;
    assert(FatigueCurve::create(FatigueCurveKind::StressLifeSN, stressUnit,
        {{293.15, {{50.0, 1.0e6}, {100.0, 1.0e5}, {200.0, 1.0e4}}}},
        curve, diagnostic));

    std::vector<LoadCycle> cycles(3);
    cycles[0].range = 200.0; cycles[0].count = 1.0; // amplitude 100, D=1e-5
    cycles[1].range = 100.0; cycles[1].count = 0.5; // amplitude 50, D=0.5e-6
    cycles[2].range = 0.0; cycles[2].count = 0.5;  // no damage

    FatigueDamageInput input;
    input.cycles = &cycles;
    input.curve = &curve;
    input.amplitudeUnit = stressUnit;
    input.temperatureKelvin = 293.15;

    PalmgrenMiner miner;
    const auto result = miner.evaluate(input);
    assert(result);
    assert(result.cyclesEvaluated == 2);
    assert(result.zeroRangeCycles == 1);
    assert(std::fabs(result.damage - 1.05e-5) < 1e-12);
    assert(std::fabs(result.blockLife - 1.0 / 1.05e-5) < 1e-5);

    auto wrongUnit = input;
    wrongUnit.amplitudeUnit.symbol = "Pa";
    wrongUnit.amplitudeUnit.scaleToSI = 1.0;
    assert(miner.evaluate(wrongUnit).status == FatigueDamageStatus::IncompatibleUnit);

    auto invalidCycles = cycles;
    invalidCycles[0].count = -1.0;
    auto invalid = input;
    invalid.cycles = &invalidCycles;
    assert(miner.evaluate(invalid).status == FatigueDamageStatus::InvalidInput);

    auto outOfRangeCycles = cycles;
    outOfRangeCycles[0].range = 1000.0;
    auto outOfRange = input;
    outOfRange.cycles = &outOfRangeCycles;
    assert(miner.evaluate(outOfRange).status == FatigueDamageStatus::CurveEvaluationFailed);

    auto emptyCycles = std::vector<LoadCycle>{};
    auto noDamage = input;
    noDamage.cycles = &emptyCycles;
    const auto zero = miner.evaluate(noDamage);
    assert(zero && zero.damage == 0.0 && std::isinf(zero.blockLife));

    return 0;
}
