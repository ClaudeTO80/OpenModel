#include <OpenModel/Fatigue/StrainLifeDamageModel.hpp>

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

bool validStressUnit(const PhysicalUnit& u) {
    return u.dimension.known && u.dimension.length == -1 &&
        u.dimension.mass == 1 && u.dimension.time == -2 &&
        u.dimension.current == 0 && u.dimension.temperature == 0 &&
        u.dimension.amount == 0 && u.dimension.luminousIntensity == 0 &&
        !u.symbol.empty() && std::isfinite(u.scaleToSI) && u.scaleToSI > 0.0 &&
        u.offsetToSI == 0.0;
}

bool validStrainUnit(const PhysicalUnit& u) {
    return u.dimension.known && u.dimension.length == 0 &&
        u.dimension.mass == 0 && u.dimension.time == 0 &&
        u.dimension.current == 0 && u.dimension.temperature == 0 &&
        u.dimension.amount == 0 && u.dimension.luminousIntensity == 0 &&
        !u.symbol.empty() && std::isfinite(u.scaleToSI) && u.scaleToSI > 0.0 &&
        u.offsetToSI == 0.0;
}

double strainLifeCurve(double reversals, const StrainLifeMaterialParameters& m,
                       StrainLifeMeanStressModel model, double meanStress) {
    const double elasticCoefficient =
        model == StrainLifeMeanStressModel::Morrow
            ? (m.fatigueStrengthCoefficient - meanStress) / m.youngsModulus
            : m.fatigueStrengthCoefficient / m.youngsModulus;
    return elasticCoefficient * std::pow(reversals, m.fatigueStrengthExponent) +
        m.fatigueDuctilityCoefficient * std::pow(reversals, m.fatigueDuctilityExponent);
}

double swtCurve(double reversals, const StrainLifeMaterialParameters& m) {
    const double sf = m.fatigueStrengthCoefficient;
    return (sf * sf / m.youngsModulus) *
            std::pow(reversals, 2.0 * m.fatigueStrengthExponent) +
        sf * m.fatigueDuctilityCoefficient *
            std::pow(reversals, m.fatigueStrengthExponent + m.fatigueDuctilityExponent);
}


} // namespace

FatigueDamageResult StrainLifeDamageModel::evaluate(const StrainLifeDamageInput& input) const {
    FatigueDamageResult result;
    if (!input.cycles || !input.material) {
        result.diagnostic = "Strain-life cycles and material parameters must be provided.";
        return result;
    }
    const auto& m = *input.material;
    if (!validStressUnit(m.stressUnit) || !validStrainUnit(m.strainUnit) ||
        !validStressUnit(input.stressUnit) || !validStrainUnit(input.strainUnit) ||
        !sameUnit(m.stressUnit, input.stressUnit) ||
        !sameUnit(m.strainUnit, input.strainUnit)) {
        result.status = FatigueDamageStatus::IncompatibleUnit;
        result.diagnostic = "Material, input stress and strain units must be valid and exactly identical; no conversion is performed.";
        return result;
    }
    if (!std::isfinite(m.youngsModulus) || m.youngsModulus <= 0.0 ||
        !std::isfinite(m.fatigueStrengthCoefficient) || m.fatigueStrengthCoefficient <= 0.0 ||
        !std::isfinite(m.fatigueStrengthExponent) || m.fatigueStrengthExponent >= 0.0 ||
        !std::isfinite(m.fatigueDuctilityCoefficient) || m.fatigueDuctilityCoefficient <= 0.0 ||
        !std::isfinite(m.fatigueDuctilityExponent) || m.fatigueDuctilityExponent >= 0.0 ||
        !std::isfinite(input.minimumReversals) || input.minimumReversals <= 0.0 ||
        !std::isfinite(input.maximumReversals) || input.maximumReversals <= input.minimumReversals) {
        result.diagnostic = "CMB material constants and reversal search bounds are invalid.";
        return result;
    }
    double sum = 0.0, compensation = 0.0;
    for (const auto& cycle : *input.cycles) {
        if (!std::isfinite(cycle.strainAmplitude) || cycle.strainAmplitude < 0.0 ||
            !std::isfinite(cycle.meanStress) || !std::isfinite(cycle.maximumStress) ||
            !std::isfinite(cycle.count) || cycle.count <= 0.0) {
            result.diagnostic = "Each strain-life cycle requires finite non-negative strain amplitude and finite stresses/count.";
            return result;
        }
        if (cycle.strainAmplitude == 0.0) {
            ++result.zeroRangeCycles;
            continue;
        }
        double target = cycle.strainAmplitude;
        if (input.meanStressModel == StrainLifeMeanStressModel::Morrow) {
            if (m.fatigueStrengthCoefficient - cycle.meanStress <= 0.0) {
                result.diagnostic = "Morrow requires sigma_f' - sigma_m > 0 for every cycle.";
                return result;
            }
        } else if (input.meanStressModel == StrainLifeMeanStressModel::SmithWatsonTopper) {
            if (cycle.maximumStress <= 0.0) {
                result.diagnostic = "SWT requires positive maximum cycle stress.";
                return result;
            }
            target *= cycle.maximumStress;
        }

        // Morrow's mean stress is cycle-specific; keep the solver stateless by
        // solving this cycle inline through a local copy of its input.
        const double lo = std::log10(input.minimumReversals);
        const double hi = std::log10(input.maximumReversals);
        const bool swt = input.meanStressModel == StrainLifeMeanStressModel::SmithWatsonTopper;
        const auto evalAt = [&](double x) {
            const double n = std::pow(10.0, x);
            return swt ? swtCurve(n, m)
                : strainLifeCurve(n, m, input.meanStressModel, cycle.meanStress);
        };
        const double yLo = evalAt(lo), yHi = evalAt(hi);
        if (!std::isfinite(yLo) || !std::isfinite(yHi) || yLo <= 0.0 || yHi <= 0.0 ||
            target > yLo || target < yHi) {
            result.status = FatigueDamageStatus::CurveEvaluationFailed;
            result.diagnostic = "Cycle is outside the configured strain-life reversal interval.";
            return result;
        }
        double a = lo, b = hi;
        for (int i = 0; i < 160; ++i) {
            const double mid = 0.5 * (a + b);
            const double value = evalAt(mid);
            if (!std::isfinite(value) || value <= 0.0) {
                result.status = FatigueDamageStatus::NumericalOverflow;
                result.diagnostic = "Numerical failure while inverting the strain-life equation.";
                return result;
            }
            if (value > target) a = mid;
            else b = mid;
        }
        const double reversals = std::pow(10.0, 0.5 * (a + b));
        const double lifeCycles = 0.5 * reversals;
        const double increment = cycle.count / lifeCycles;
        if (!std::isfinite(increment) || increment < 0.0) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Strain-life damage increment is not finite.";
            return result;
        }
        const double y = increment - compensation;
        const double next = sum + y;
        compensation = (next - sum) - y;
        if (!std::isfinite(next)) {
            result.status = FatigueDamageStatus::NumericalOverflow;
            result.diagnostic = "Accumulated strain-life damage overflowed.";
            return result;
        }
        sum = next;
        ++result.cyclesEvaluated;
    }
    result.damage = sum;
    result.blockLife = sum > 0.0 ? 1.0 / sum : std::numeric_limits<double>::infinity();
    result.status = FatigueDamageStatus::Success;
    result.diagnostic = "Coffin-Manson-Basquin strain-life Miner damage evaluated with the selected mean-stress model.";
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
