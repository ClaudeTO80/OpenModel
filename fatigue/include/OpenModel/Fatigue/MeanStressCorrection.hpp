#pragma once

#include <cmath>
#include <string>

namespace OpenModel {
namespace Fatigue {

enum class MeanStressCorrectionKind { None, Goodman, Gerber, Soderberg, Morrow, SmithWatsonTopper };

enum class MeanStressCorrectionStatus {
    Success, InvalidInput, MissingMaterialProperty, NonPositiveDenominator,
    OutsideMaterialStrength, NonTensileMaximumStress
};

struct MeanStressCorrectionParameters {
    // Required by Goodman/Gerber; Morrow uses true fracture strength (or an
    // explicitly calibrated equivalent reference strength). Units match stress.
    double ultimateTensileStrength = 0.0;
    // Required by Soderberg.
    double yieldStrength = 0.0;
    // Required by Morrow.
    double trueFractureStrength = 0.0;
};

struct MeanStressCorrectionResult {
    MeanStressCorrectionStatus status = MeanStressCorrectionStatus::InvalidInput;
    double correctedAmplitude = 0.0;
    double originalAmplitude = 0.0;
    double meanStress = 0.0;
    double maximumStress = 0.0;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == MeanStressCorrectionStatus::Success; }
};

/**
 * Uniaxial cycle-level mean-stress corrections for stress-life (S-N) use.
 * All stresses and strengths must use the same unit. Compression is retained
 * algebraically; no automatic tension-only clipping is applied.
 */
class MeanStressCorrection final {
public:
    static MeanStressCorrectionResult apply(
        MeanStressCorrectionKind kind,
        double stressAmplitude,
        double meanStress,
        const MeanStressCorrectionParameters& parameters = {});
};

} // namespace Fatigue
} // namespace OpenModel
