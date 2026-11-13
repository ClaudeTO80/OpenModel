#pragma once

#include <OpenModel/Fatigue/StrainLifeDamageModel.hpp>

namespace OpenModel {
namespace Fatigue {

/**
 * One already-counted Wang-Brown multiaxial cycle.
 *
 * The cycle counter must supply the maximum shear-strain amplitude and the
 * associated normal-strain excursion for the same cycle. The two quantities
 * are dimensionless strain amplitudes/ranges, not tensor components.
 */
struct WangBrownCycle {
    double maximumShearStrainAmplitude = 0.0;
    double normalStrainExcursion = 0.0;
    double count = 1.0;
    std::size_t startSampleIndex = 0;
    std::size_t endSampleIndex = 0;
};

/** Wang-Brown 1993 material constants for one calibrated material condition. */
struct WangBrownMaterialParameters {
    StrainLifeMaterialParameters strainLife;
    double nonProportionalityCoefficient = 0.0; // S, dimensionless
    double elasticPoissonRatio = 0.0;            // nu_e
    double plasticPoissonRatio = 0.0;            // nu_p
};

struct WangBrownDamageInput {
    const std::vector<WangBrownCycle>* cycles = nullptr;
    const WangBrownMaterialParameters* material = nullptr;
    PhysicalUnit strainUnit;
    // Search interval in reversals (2*Nf), not cycles.
    double minimumReversals = 1.0;
    double maximumReversals = 1.0e16;
};

/**
 * Original Wang-Brown (1993) parameter/life relation with linear Miner
 * accumulation. This class evaluates cycles already identified by a
 * compatible multiaxial counter; it does not perform path segmentation.
 *
 * P_WB = gamma_a,max + S * epsilon_n*
 * P_WB = C1*(sigma_f'/E)*(2*Nf)^b + C2*epsilon_f'*(2*Nf)^c
 * C1 = 1 + nu_e + (1 - nu_e)*S
 * C2 = 1 + nu_p + (1 - nu_p)*S
 */
class WangBrownDamageModel final {
public:
    std::string name() const { return "Wang-Brown (1993) strain-life Miner"; }
    FatigueDamageResult evaluate(const WangBrownDamageInput& input) const;
};

} // namespace Fatigue
} // namespace OpenModel
