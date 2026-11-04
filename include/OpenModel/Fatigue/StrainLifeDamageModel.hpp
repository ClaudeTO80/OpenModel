#pragma once

#include <OpenModel/Fatigue/FatigueDamageModel.hpp>

namespace OpenModel {
namespace Fatigue {

enum class StrainLifeMeanStressModel { None, Morrow, SmithWatsonTopper };

/** Coffin-Manson-Basquin cyclic fatigue constants for one material condition. */
struct StrainLifeMaterialParameters {
    PhysicalUnit stressUnit;
    PhysicalUnit strainUnit;
    double youngsModulus = 0.0;             // stress units
    double fatigueStrengthCoefficient = 0.0; // sigma_f', stress units
    double fatigueStrengthExponent = 0.0;  // b < 0
    double fatigueDuctilityCoefficient = 0.0; // epsilon_f', dimensionless
    double fatigueDuctilityExponent = 0.0;  // c < 0
};

/** One uniaxial strain-life cycle. Stresses are signed and use stressUnit. */
struct StrainLifeCycle {
    double strainAmplitude = 0.0; // total strain amplitude, not range
    double meanStress = 0.0;
    double maximumStress = 0.0;
    double count = 1.0; // Rainflow weight: normally 0.5 or 1.0
    std::size_t startSampleIndex = 0;
    std::size_t endSampleIndex = 0;
};

struct StrainLifeDamageInput {
    const std::vector<StrainLifeCycle>* cycles = nullptr;
    const StrainLifeMaterialParameters* material = nullptr;
    PhysicalUnit strainUnit;
    PhysicalUnit stressUnit;
    StrainLifeMeanStressModel meanStressModel = StrainLifeMeanStressModel::None;
    // Numerical search interval in reversals (2*Nf), inclusive.
    double minimumReversals = 1.0;
    double maximumReversals = 1.0e16;
};

/**
 * Coffin-Manson-Basquin strain-life Miner model with Morrow and SWT.
 * The inverse life equation is solved in ln(reversals) by a safeguarded Newton
 * iteration (see TwoTermSolver.hpp).
 * This model is uniaxial and does not perform cyclic stress-strain conversion.
 */
class StrainLifeDamageModel final {
public:
    std::string name() const { return "Strain-life Palmgren-Miner"; }
    FatigueDamageResult evaluate(const StrainLifeDamageInput& input) const;
};

} // namespace Fatigue
} // namespace OpenModel
