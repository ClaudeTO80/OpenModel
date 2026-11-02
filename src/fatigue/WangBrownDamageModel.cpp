#include <OpenModel/Fatigue/WangBrownDamageModel.hpp>

#include <cmath>
#include <limits>

namespace OpenModel {
namespace Fatigue {
namespace {

bool sameDimension(const PhysicalDimension& a, const PhysicalDimension& b) {
    return a.known == b.known && a.length == b.length && a.mass == b.mass &&
        a.time == b.time && a.current == b.current &&
        a.temperature == b.temperature && a.amount == b.amount &&
        a.luminousIntensity == b.luminousIntensity;
}

bool sameUnit(const PhysicalUnit& a, const PhysicalUnit& b) {
    return sameDimension(a.dimension, b.dimension) && a.symbol == b.symbol &&
        a.scaleToSI == b.scaleToSI && a.offsetToSI == b.offsetToSI;
}

bool validStrainUnit(const PhysicalUnit& u) {
    return u.dimension.known && u.dimension.length == 0 &&
        u.dimension.mass == 0 && u.dimension.time == 0 &&
        u.dimension.current == 0 && u.dimension.temperature == 0 &&
        u.dimension.amount == 0 && u.dimension.luminousIntensity == 0 &&
        !u.symbol.empty() && std::isfinite(u.scaleToSI) && u.scaleToSI > 0.0 &&
        u.offsetToSI == 0.0;
}

} // namespace

FatigueDamageResult WangBrownDamageModel::evaluate(const WangBrownDamageInput& input) const {
    FatigueDamageResult result;
    if (!input.cycles || !input.material) {
        result.diagnostic = "Wang-Brown cycles and material parameters must be provided.";
        return result;
    }

    const auto& m = *input.material;
    const auto& sl = m.strainLife;
    if (!validStrainUnit(input.strainUnit) || !validStrainUnit(sl.strainUnit) ||
        !sameUnit(input.strainUnit, sl.strainUnit)) {
        result.status = FatigueDamageStatus::IncompatibleUnit;
        result.diagnostic = "Wang-Brown material and cycle strain units must be identical dimensionless units.";
        return result;
    }
    if (!sl.stressUnit.dimension.known ||
        sl.stressUnit.dimension.length != -1 || sl.stressUnit.dimension.mass != 1 ||
        sl.stressUnit.dimension.time != -2 || sl.stressUnit.dimension.current != 0 ||
        sl.stressUnit.dimension.temperature != 0 || sl.stressUnit.dimension.amount != 0 ||
        sl.stressUnit.dimension.luminousIntensity != 0 || sl.stressUnit.symbol.empty() ||
        !std::isfinite(sl.stressUnit.scaleToSI) || sl.stressUnit.scaleToSI <= 0.0 ||
        sl.stressUnit.offsetToSI != 0.0 ||
        !std::isfinite(sl.youngsModulus) || sl.youngsModulus <= 0.0 ||
        !std::isfinite(sl.fatigueStrengthCoefficient) || sl.fatigueStrengthCoefficient <= 0.0 ||
        !std::isfinite(sl.fatigueStrengthExponent) || sl.fatigueStrengthExponent >= 0.0 ||
        !std::isfinite(sl.fatigueDuctilityCoefficient) || sl.fatigueDuctilityCoefficient <= 0.0 ||
        !std::isfinite(sl.fatigueDuctilityExponent) || sl.fatigueDuctilityExponent >= 0.0 ||
        !std::isfinite(m.nonProportionalityCoefficient) || m.nonProportionalityCoefficient < 0.0 ||
        !std::isfinite(m.elasticPoissonRatio) || m.elasticPoissonRatio <= -1.0 ||
        m.elasticPoissonRatio >= 0.5 ||
        !std::isfinite(m.plasticPoissonRatio) || m.plasticPoissonRatio <= -1.0 ||
        m.plasticPoissonRatio >= 0.5 ||
        !std::isfinite(input.minimumReversals) || input.minimumReversals <= 0.0 ||
        !std::isfinite(input.maximumReversals) ||
        input.maximumReversals <= input.minimumReversals) {
        result.diagnostic = "Wang-Brown material constants, Poisson ratios, or reversal bounds are invalid.";
        return result;
    }

    const double S = m.nonProportionalityCoefficient;
    const double C1 = 1.0 + m.elasticPoissonRatio + (1.0 - m.elasticPoissonRatio) * S;
    const double C2 = 1.0 + m.plasticPoissonRatio + (1.0 - m.plasticPoissonRatio) * S;
    if (!std::isfinite(C1) || C1 <= 0.0 || !std::isfinite(C2) || C2 <= 0.0) {
        result.diagnostic = "Wang-Brown C1 and C2 coefficients must be finite and positive.";
        return result;
    }

    const double lo = std::log10(input.minimumReversals);
    const double hi = std::log10(input.maximumReversals);
    double sum = 0.0;
    double compensation = 0.0;

    for (const auto& cycle : *input.cycles) {
        if (!std::isfinite(cycle.maximumShearStrainAmplitude) ||
            cycle.maximumShearStrainAmplitude < 0.0 ||
            !std::isfinite(cycle.normalStrainExcursion) ||
            cycle.normalStrainExcursion < 0.0 ||
            !std::isfinite(cycle.count) || cycle.count <= 0.0) {
            result.diagnostic = "Each Wang-Brown cycle requires finite non-negative strain measures and a positive count.";
            return result;
        }
        const double parameter = cycle.maximumShearStrainAmplitude +
            S * cycle.normalStrainExcursion;
        if (!std::isfinite(parameter)) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Wang-Brown damage parameter overflowed.";
            return result;
        }
        if (parameter == 0.0) {
            ++result.zeroRangeCycles;
            continue;
        }

        const auto curve = [&](double logReversals) {
            const double reversals = std::pow(10.0, logReversals);
            return C1 * (sl.fatigueStrengthCoefficient / sl.youngsModulus) *
                       std::pow(reversals, sl.fatigueStrengthExponent) +
                   C2 * sl.fatigueDuctilityCoefficient *
                       std::pow(reversals, sl.fatigueDuctilityExponent);
        };

        const double atLow = curve(lo);
        const double atHigh = curve(hi);
        if (!std::isfinite(atLow) || !std::isfinite(atHigh) ||
            atLow <= 0.0 || atHigh <= 0.0 ||
            parameter > atLow || parameter < atHigh) {
            result.status = FatigueDamageStatus::CurveEvaluationFailed;
            result.diagnostic = "Wang-Brown cycle parameter is outside the configured strain-life reversal interval.";
            return result;
        }

        double a = lo;
        double b = hi;
        for (int iteration = 0; iteration < 160; ++iteration) {
            const double mid = 0.5 * (a + b);
            const double value = curve(mid);
            if (!std::isfinite(value) || value <= 0.0) {
                result.status = FatigueDamageStatus::NumericalOverflow;
                result.diagnostic = "Numerical failure while inverting the Wang-Brown life equation.";
                return result;
            }
            if (value > parameter) a = mid;
            else b = mid;
        }

        const double reversals = std::pow(10.0, 0.5 * (a + b));
        const double lifeCycles = 0.5 * reversals;
        const double increment = cycle.count / lifeCycles;
        if (!std::isfinite(increment) || increment < 0.0) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Wang-Brown Miner damage increment is not finite.";
            return result;
        }
        const double adjusted = increment - compensation;
        const double next = sum + adjusted;
        compensation = (next - sum) - adjusted;
        if (!std::isfinite(next)) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Accumulated Wang-Brown damage overflowed.";
            return result;
        }
        sum = next;
        ++result.cyclesEvaluated;
    }

    result.damage = sum;
    result.blockLife = sum > 0.0 ? 1.0 / sum : std::numeric_limits<double>::infinity();
    result.status = FatigueDamageStatus::Success;
    result.diagnostic = "Original Wang-Brown (1993) strain-life parameter and linear Miner damage evaluated.";
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
