#include <OpenModel/Fatigue/FatigueDamageModel.hpp>

#include <cmath>
#include <limits>

namespace OpenModel {
namespace Fatigue {
namespace {

bool sameDimension(const PhysicalDimension& a, const PhysicalDimension& b) {
    return a.known == b.known &&
        a.length == b.length && a.mass == b.mass && a.time == b.time &&
        a.current == b.current && a.temperature == b.temperature &&
        a.amount == b.amount && a.luminousIntensity == b.luminousIntensity;
}

bool sameUnit(const PhysicalUnit& a, const PhysicalUnit& b) {
    return sameDimension(a.dimension, b.dimension) &&
        a.symbol == b.symbol && a.scaleToSI == b.scaleToSI &&
        a.offsetToSI == b.offsetToSI;
}

} // namespace

FatigueDamageResult PalmgrenMiner::evaluate(const FatigueDamageInput& input) const {
    FatigueDamageResult result;
    if (!input.cycles || !input.curve) {
        result.diagnostic = "Cycle list and fatigue curve must be provided.";
        return result;
    }
    if (input.curve->kind() != FatigueCurveKind::StressLifeSN) {
        result.status = FatigueDamageStatus::IncompatibleQuantity;
        result.diagnostic = "Palmgren-Miner currently requires an S-N stress-life curve.";
        return result;
    }
    if (!sameUnit(input.amplitudeUnit, input.curve->amplitudeUnit())) {
        result.status = FatigueDamageStatus::IncompatibleUnit;
        result.diagnostic = "Input stress unit must exactly match the S-N curve amplitude unit; no conversion is performed.";
        return result;
    }
    if (!std::isfinite(input.temperatureKelvin) || input.temperatureKelvin <= 0.0) {
        result.diagnostic = "Temperature must be finite, positive and expressed in kelvin.";
        return result;
    }

    double sum = 0.0;
    double compensation = 0.0;
    for (const auto& cycle : *input.cycles) {
        if (!std::isfinite(cycle.range) || cycle.range < 0.0 ||
            !std::isfinite(cycle.count) || cycle.count <= 0.0 ||
            !std::isfinite(cycle.mean)) {
            result.diagnostic = "Every cycle must have finite mean, non-negative range and positive finite count.";
            return result;
        }
        if (cycle.range == 0.0) {
            ++result.zeroRangeCycles;
            continue;
        }

        const double amplitude = 0.5 * cycle.range;
        if (!std::isfinite(amplitude) || amplitude <= 0.0) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Cycle amplitude is not representable.";
            return result;
        }
        const auto correction = MeanStressCorrection::apply(
            input.meanStressCorrection, amplitude, cycle.mean, input.meanStressParameters);
        if (!correction) {
            result.status = FatigueDamageStatus::InvalidInput;
            result.diagnostic = "Mean-stress correction failed: " + correction.diagnostic;
            return result;
        }
        const auto life = input.curve->evaluate(correction.correctedAmplitude, input.temperatureKelvin);
        if (!life) {
            result.status = FatigueDamageStatus::CurveEvaluationFailed;
            result.diagnostic = "S-N curve evaluation failed: " + life.diagnostic;
            return result;
        }

        const double increment = cycle.count / life.cyclesToFailure;
        if (!std::isfinite(increment) || increment < 0.0) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Cycle damage increment is not finite.";
            return result;
        }

        // Kahan compensated summation limits loss of small increments.
        const double adjustedIncrement = increment - compensation;
        const double next = sum + adjustedIncrement;
        compensation = (next - sum) - adjustedIncrement;
        if (!std::isfinite(next)) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Accumulated damage overflowed.";
            return result;
        }
        sum = next;
        ++result.cyclesEvaluated;
    }

    result.damage = sum;
    result.blockLife = sum > 0.0 ? 1.0 / sum : std::numeric_limits<double>::infinity();
    result.status = FatigueDamageStatus::Success;
    result.diagnostic = input.meanStressCorrection == MeanStressCorrectionKind::None
        ? "Linear Palmgren-Miner damage evaluated without mean-stress correction or sequence effects."
        : "Linear Palmgren-Miner damage evaluated after the selected uniaxial mean-stress correction; sequence effects were not considered.";
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
