#include <OpenModel/Fatigue/WangBrownDamageModel.hpp>

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

    WangBrownMaterialParameters material;
    material.strainLife.stressUnit = stress;
    material.strainLife.strainUnit = strain;
    material.strainLife.youngsModulus = 210000.0;
    material.strainLife.fatigueStrengthCoefficient = 1000.0;
    material.strainLife.fatigueStrengthExponent = -0.1;
    material.strainLife.fatigueDuctilityCoefficient = 0.5;
    material.strainLife.fatigueDuctilityExponent = -0.5;
    material.nonProportionalityCoefficient = 0.25;
    material.elasticPoissonRatio = 0.3;
    material.plasticPoissonRatio = 0.5 - 1e-6;

    std::vector<WangBrownCycle> cycles{{0.004, 0.002, 1.0, 0, 4}};
    WangBrownDamageInput input;
    input.cycles = &cycles;
    input.material = &material;
    input.strainUnit = strain;

    WangBrownDamageModel model;
    const auto result = model.evaluate(input);
    assert(result);
    assert(result.damage > 0.0);
    assert(result.cyclesEvaluated == 1);
    assert(result.blockLife > 0.0);

    const double baseDamage = result.damage;
    material.nonProportionalityCoefficient = 0.0;
    const auto withoutNormalExcursion = model.evaluate(input);
    assert(withoutNormalExcursion);
    assert(withoutNormalExcursion.damage < baseDamage);

    material.nonProportionalityCoefficient = 0.25;
    cycles[0].normalStrainExcursion = 0.0;
    const auto withoutExcursion = model.evaluate(input);
    assert(withoutExcursion);
    assert(withoutExcursion.damage < baseDamage);

    cycles[0].maximumShearStrainAmplitude = -1.0;
    assert(model.evaluate(input).status == FatigueDamageStatus::InvalidInput);
    cycles[0].maximumShearStrainAmplitude = 0.004;
    input.strainUnit.symbol = "percent";
    assert(model.evaluate(input).status == FatigueDamageStatus::IncompatibleUnit);

    input.strainUnit = strain;
    material.elasticPoissonRatio = 0.5;
    assert(model.evaluate(input).status == FatigueDamageStatus::InvalidInput);
    return 0;
}
