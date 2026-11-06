#include <OpenModel/Fatigue/LocalStrainCorrection.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    PhysicalUnit stress;
    stress.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    stress.symbol = "MPa";
    stress.scaleToSI = 1.0e6;
    PhysicalUnit strain;
    strain.dimension = {true, 0, 0, 0, 0, 0, 0, 0};
    strain.symbol = "1";
    strain.scaleToSI = 1.0;

    CyclicStressStrainParameters material;
    material.stressUnit = stress;
    material.strainUnit = strain;
    material.youngsModulus = 210000.0;
    material.cyclicStrengthCoefficient = 1000.0;
    material.cyclicStrainHardeningExponent = 0.15;

    std::vector<ElasticStrainLifeCycle> elastic{{250.0, 50.0, 1.0, 2, 8}};
    const auto neuber = LocalStrainCorrection::convert(
        elastic, material, LocalStrainCorrectionMethod::Neuber);
    assert(neuber && neuber.cycles.size() == 1);
    assert(neuber.cycles[0].strainAmplitude > 250.0 / material.youngsModulus);
    assert(neuber.cycles[0].maximumStress > 50.0);
    assert(neuber.cycles[0].startSampleIndex == 2);
    assert(neuber.cycles[0].endSampleIndex == 8);

    const auto glinka = LocalStrainCorrection::convert(
        elastic, material, LocalStrainCorrectionMethod::Glinka);
    assert(glinka && glinka.cycles.size() == 1);
    assert(glinka.cycles[0].strainAmplitude > 0.0);
    assert(std::isfinite(glinka.cycles[0].strainAmplitude));

    // Reference values computed independently (Python bisection on
    // sigma^2/(2E) + sigma*(sigma/K')^(1/n')/(1+n') = sigma_e^2/(2E)).
    assert(std::fabs(glinka.cycles[0].strainAmplitude - 0.001201019528) < 1e-9);
    assert(std::fabs(neuber.cycles[0].strainAmplitude - 0.00123006456) < 1e-9);

    // Glinka must satisfy the strain-energy equality int sigma d(eps) = sigma_e^2/(2E),
    // verified by trapezoidal integration of the Ramberg-Osgood curve itself.
    {
        const double E = material.youngsModulus;
        const double K = material.cyclicStrengthCoefficient;
        const double n = material.cyclicStrainHardeningExponent;
        const auto strainOf = [&](double s) { return s / E + std::pow(s / K, 1.0 / n); };
        const double eps = glinka.cycles[0].strainAmplitude;
        // Recover the local stress amplitude from the maximum stress (mean = 50).
        const double sigmaLocal = glinka.cycles[0].maximumStress - 50.0;
        assert(std::fabs(strainOf(sigmaLocal) - eps) < 1e-12);
        const int steps = 200000;
        double energy = 0.0, prevS = 0.0, prevE = 0.0;
        for (int i = 1; i <= steps; ++i) {
            const double s = sigmaLocal * i / steps;
            const double e = strainOf(s);
            energy += 0.5 * (prevS + s) * (e - prevE);
            prevS = s; prevE = e;
        }
        const double target = 250.0 * 250.0 / (2.0 * E);
        assert(std::fabs(energy - target) / target < 1e-6);
    }

    // Known ordering: Glinka predicts less local strain than Neuber for n' < 1.
    for (double amplitude : {100.0, 250.0, 400.0, 600.0, 900.0}) {
        std::vector<ElasticStrainLifeCycle> one{{amplitude, 0.0, 1.0, 0, 1}};
        const auto n = LocalStrainCorrection::convert(one, material, LocalStrainCorrectionMethod::Neuber);
        const auto g = LocalStrainCorrection::convert(one, material, LocalStrainCorrectionMethod::Glinka);
        assert(n && g);
        assert(g.cycles[0].strainAmplitude < n.cycles[0].strainAmplitude);
        // In the elastic limit both rules collapse to eps = sigma_e / E.
        assert(g.cycles[0].strainAmplitude >= amplitude / material.youngsModulus);
    }

    LoadCycle rainflow;
    rainflow.range = 500.0;
    rainflow.mean = 50.0;
    rainflow.count = 0.5;
    rainflow.start.historySampleIndex = 3;
    rainflow.end.historySampleIndex = 9;
    const auto bridged = LocalStrainCorrection::convert(
        std::vector<LoadCycle>{rainflow}, stress, material);
    assert(bridged && bridged.cycles.size() == 1);
    assert(bridged.cycles[0].count == 0.5);
    assert(bridged.cycles[0].startSampleIndex == 3);

    PhysicalUnit wrongStress = stress;
    wrongStress.symbol = "Pa";
    assert(LocalStrainCorrection::convert(
        std::vector<LoadCycle>{rainflow}, wrongStress, material).status ==
        LocalStrainCorrectionStatus::IncompatibleUnit);

    material.cyclicStrainHardeningExponent = 0.0;
    assert(LocalStrainCorrection::convert(elastic, material).status ==
        LocalStrainCorrectionStatus::InvalidInput);
    return 0;
}
