#pragma once

#include <OpenModel/Fatigue/StrainLifeDamageModel.hpp>
#include <OpenModel/Fatigue/CycleCounting.hpp>

#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

/** Local cyclic stress-strain law: epsilon_a = sigma_a/E + (sigma_a/K')^(1/n'). */
struct CyclicStressStrainParameters {
    PhysicalUnit stressUnit;
    PhysicalUnit strainUnit;
    double youngsModulus = 0.0;
    double cyclicStrengthCoefficient = 0.0; // K', stress units
    double cyclicStrainHardeningExponent = 0.0; // n', 0 < n' <= 1 typically
};

enum class LocalStrainCorrectionMethod { Neuber, Glinka };

enum class LocalStrainCorrectionStatus {
    Success,
    InvalidInput,
    IncompatibleUnit,
    NumericalFailure
};

/** Elastic cycle data from a linear-elastic FEM stress history and Rainflow. */
struct ElasticStrainLifeCycle {
    double elasticStressAmplitude = 0.0;
    double elasticMeanStress = 0.0;
    double count = 1.0;
    std::size_t startSampleIndex = 0;
    std::size_t endSampleIndex = 0;
};

struct LocalStrainCorrectionResult {
    LocalStrainCorrectionStatus status = LocalStrainCorrectionStatus::InvalidInput;
    std::vector<StrainLifeCycle> cycles;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == LocalStrainCorrectionStatus::Success; }
};

/**
 * Converts elastic uniaxial cycle amplitudes to stabilized local strain cycles
 * using the cyclic Ramberg-Osgood law and Neuber or Glinka energy correction.
 *
 * The supplied elastic mean stress is retained as an approximation; only the
 * alternating component is locally corrected. This is not a residual-stress,
 * ratcheting, or non-proportional multiaxial solver.
 */
class LocalStrainCorrection final {
public:
    static LocalStrainCorrectionResult convert(
        const std::vector<LoadCycle>& cycles,
        const PhysicalUnit& stressUnit,
        const CyclicStressStrainParameters& material,
        LocalStrainCorrectionMethod method = LocalStrainCorrectionMethod::Neuber);

    static LocalStrainCorrectionResult convert(
        const std::vector<ElasticStrainLifeCycle>& cycles,
        const CyclicStressStrainParameters& material,
        LocalStrainCorrectionMethod method = LocalStrainCorrectionMethod::Neuber);
};

} // namespace Fatigue
} // namespace OpenModel
