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
