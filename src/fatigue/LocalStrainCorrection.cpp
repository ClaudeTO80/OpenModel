#include <OpenModel/Fatigue/LocalStrainCorrection.hpp>

#include <cmath>

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

double cyclicStrain(double stress, const CyclicStressStrainParameters& m) {
    return stress / m.youngsModulus +
        std::pow(stress / m.cyclicStrengthCoefficient,
                 1.0 / m.cyclicStrainHardeningExponent);
}

double glinkaEnergy(double stress, const CyclicStressStrainParameters& m) {
    const double n = m.cyclicStrainHardeningExponent;
    const double elastic = stress * stress / (2.0 * m.youngsModulus);
    const double plastic = m.cyclicStrengthCoefficient *
        std::pow(stress / m.cyclicStrengthCoefficient, 1.0 + 1.0 / n) /
        (1.0 + 1.0 / n);
    return elastic + plastic;
}

bool solveMonotone(double target, double upper,
                   const CyclicStressStrainParameters& m,
                   LocalStrainCorrectionMethod method, double& stress) {
    double lo = 0.0;
    double hi = upper;
    const auto response = [&](double x) {
        return method == LocalStrainCorrectionMethod::Neuber
            ? x * cyclicStrain(x, m) : glinkaEnergy(x, m);
    };
    double hiValue = response(hi);
    if (!std::isfinite(hiValue) || hiValue < target) {
        for (int i = 0; i < 64 && (!std::isfinite(hiValue) || hiValue < target); ++i) {
            hi *= 2.0;
            if (!std::isfinite(hi)) return false;
            hiValue = response(hi);
        }
    }
    if (!std::isfinite(hiValue) || hiValue < target) return false;
    for (int i = 0; i < 160; ++i) {
        const double mid = 0.5 * (lo + hi);
        const double value = response(mid);
        if (!std::isfinite(value)) return false;
        if (value < target) lo = mid;
        else hi = mid;
    }
    stress = 0.5 * (lo + hi);
    return std::isfinite(stress) && stress > 0.0;
}

} // namespace

LocalStrainCorrectionResult LocalStrainCorrection::convert(
    const std::vector<LoadCycle>& cycles,
    const PhysicalUnit& stressUnit,
    const CyclicStressStrainParameters& material,
    LocalStrainCorrectionMethod method) {
    if (!sameUnit(stressUnit, material.stressUnit)) {
        LocalStrainCorrectionResult result;
        result.status = LocalStrainCorrectionStatus::IncompatibleUnit;
        result.diagnostic = "Rainflow stress unit must exactly match the cyclic material stress unit.";
        return result;
    }
    std::vector<ElasticStrainLifeCycle> elasticCycles;
    elasticCycles.reserve(cycles.size());
    for (const auto& cycle : cycles) {
        elasticCycles.push_back({0.5 * cycle.range, cycle.mean, cycle.count,
            cycle.start.historySampleIndex, cycle.end.historySampleIndex});
    }
    return convert(elasticCycles, material, method);
}

LocalStrainCorrectionResult LocalStrainCorrection::convert(
    const std::vector<ElasticStrainLifeCycle>& cycles,
    const CyclicStressStrainParameters& material,
    LocalStrainCorrectionMethod method) {
    LocalStrainCorrectionResult result;
    if (!material.stressUnit.dimension.known ||
        material.stressUnit.dimension.length != -1 ||
        material.stressUnit.dimension.mass != 1 ||
        material.stressUnit.dimension.time != -2 ||
        material.stressUnit.dimension.current != 0 ||
        material.stressUnit.dimension.temperature != 0 ||
        material.stressUnit.dimension.amount != 0 ||
        material.stressUnit.dimension.luminousIntensity != 0 ||
        material.stressUnit.symbol.empty() || !std::isfinite(material.stressUnit.scaleToSI) ||
        material.stressUnit.scaleToSI <= 0.0 || material.stressUnit.offsetToSI != 0.0 ||
        !material.strainUnit.dimension.known ||
        material.strainUnit.dimension.length != 0 ||
        material.strainUnit.dimension.mass != 0 ||
        material.strainUnit.dimension.time != 0 ||
        material.strainUnit.dimension.current != 0 ||
        material.strainUnit.dimension.temperature != 0 ||
        material.strainUnit.dimension.amount != 0 ||
        material.strainUnit.dimension.luminousIntensity != 0 ||
        material.strainUnit.symbol.empty() || !std::isfinite(material.strainUnit.scaleToSI) ||
        material.strainUnit.scaleToSI <= 0.0 || material.strainUnit.offsetToSI != 0.0 ||
        (method != LocalStrainCorrectionMethod::Neuber && method != LocalStrainCorrectionMethod::Glinka) ||
        !std::isfinite(material.youngsModulus) || material.youngsModulus <= 0.0 ||
        !std::isfinite(material.cyclicStrengthCoefficient) ||
        material.cyclicStrengthCoefficient <= 0.0 ||
        !std::isfinite(material.cyclicStrainHardeningExponent) ||
        material.cyclicStrainHardeningExponent <= 0.0 ||
        material.cyclicStrainHardeningExponent > 1.0) {
        result.diagnostic = "Cyclic Ramberg-Osgood material parameters or units are invalid.";
        return result;
    }
    result.cycles.reserve(cycles.size());
    for (const auto& cycle : cycles) {
        if (!std::isfinite(cycle.elasticStressAmplitude) ||
            cycle.elasticStressAmplitude < 0.0 ||
            !std::isfinite(cycle.elasticMeanStress) ||
            !std::isfinite(cycle.count) || cycle.count <= 0.0) {
            result.diagnostic = "Elastic cycles require finite non-negative amplitude, finite mean, positive count and ordered sample indices.";
            return result;
        }
        if (cycle.elasticStressAmplitude == 0.0) {
            result.cycles.push_back({0.0, cycle.elasticMeanStress,
                cycle.elasticMeanStress, cycle.count,
                cycle.startSampleIndex, cycle.endSampleIndex});
            continue;
        }
        const double elastic = cycle.elasticStressAmplitude;
        const double target = method == LocalStrainCorrectionMethod::Neuber
            ? elastic * elastic / material.youngsModulus
            : elastic * elastic / (2.0 * material.youngsModulus);
        double localStress = 0.0;
        if (!solveMonotone(target, elastic, material, method, localStress)) {
            result.status = LocalStrainCorrectionStatus::NumericalFailure;
            result.diagnostic = "Could not solve the local cyclic stress correction.";
            result.cycles.clear();
            return result;
        }
        const double strainAmplitude = cyclicStrain(localStress, material);
        const double maximumStress = cycle.elasticMeanStress + localStress;
        if (!std::isfinite(strainAmplitude) || !std::isfinite(maximumStress)) {
            result.status = LocalStrainCorrectionStatus::NumericalFailure;
            result.diagnostic = "Local strain correction produced a non-finite result.";
            result.cycles.clear();
            return result;
        }
        result.cycles.push_back({strainAmplitude, cycle.elasticMeanStress,
            maximumStress, cycle.count, cycle.startSampleIndex, cycle.endSampleIndex});
    }
    result.status = LocalStrainCorrectionStatus::Success;
    result.diagnostic = "Elastic cycles converted with cyclic Ramberg-Osgood and the selected local correction.";
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
