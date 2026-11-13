#include <OpenModel/Fatigue/StrainLifeDamageModel.hpp>
#include <cassert>
#include <cmath>
using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    PhysicalUnit stress; stress.dimension={true,-1,1,-2,0,0,0,0}; stress.symbol="MPa"; stress.scaleToSI=1.0e6;
    PhysicalUnit strain; strain.dimension={true,0,0,0,0,0,0,0}; strain.symbol="1"; strain.scaleToSI=1.0;
    StrainLifeMaterialParameters material;
    material.stressUnit=stress; material.strainUnit=strain;
    material.youngsModulus=210000.0; material.fatigueStrengthCoefficient=1000.0;
    material.fatigueStrengthExponent=-0.1; material.fatigueDuctilityCoefficient=0.5;
    material.fatigueDuctilityExponent=-0.5;
    std::vector<StrainLifeCycle> cycles{{0.005,0.0,100.0,1.0,0,1}};
    StrainLifeDamageInput input; input.cycles=&cycles; input.material=&material;
    input.stressUnit=stress; input.strainUnit=strain;
    StrainLifeDamageModel model;
    auto base=model.evaluate(input); assert(base && base.damage>0.0);
    input.meanStressModel=StrainLifeMeanStressModel::Morrow;
    cycles[0].meanStress=100.0;
    auto morrow=model.evaluate(input); assert(morrow && morrow.damage>base.damage);
    input.meanStressModel=StrainLifeMeanStressModel::SmithWatsonTopper;
    cycles[0].maximumStress=200.0;
    auto swt=model.evaluate(input); assert(swt && swt.damage>0.0);
    cycles[0].maximumStress=0.0;
    assert(model.evaluate(input).status==FatigueDamageStatus::InvalidInput);
    cycles[0].maximumStress=200.0;
    input.stressUnit.symbol="Pa";
    assert(model.evaluate(input).status==FatigueDamageStatus::IncompatibleUnit);
    return 0;
}
