#include <OpenModel/Fatigue/MeanStressCorrection.hpp>

#include <algorithm>
#include <cmath>

namespace OpenModel {
namespace Fatigue {
namespace {
MeanStressCorrectionResult failure(MeanStressCorrectionStatus status, const char* message) {
    MeanStressCorrectionResult r;
    r.status = status;
    r.diagnostic = message;
    return r;
}
}

MeanStressCorrectionResult MeanStressCorrection::apply(
    MeanStressCorrectionKind kind, double amplitude, double mean,
    const MeanStressCorrectionParameters& p) {
    if (!std::isfinite(amplitude) || amplitude < 0.0 || !std::isfinite(mean))
        return failure(MeanStressCorrectionStatus::InvalidInput,
            "Stress amplitude must be finite and non-negative and mean stress finite.");

    MeanStressCorrectionResult r;
    r.originalAmplitude = amplitude;
    r.meanStress = mean;
    r.maximumStress = mean + amplitude;
    if (!std::isfinite(r.maximumStress))
        return failure(MeanStressCorrectionStatus::InvalidInput, "Maximum cycle stress overflowed.");

    double denominator = 1.0;
    switch (kind) {
    case MeanStressCorrectionKind::None:
        r.correctedAmplitude = amplitude;
        break;
    case MeanStressCorrectionKind::Goodman:
        if (!std::isfinite(p.ultimateTensileStrength) || p.ultimateTensileStrength <= 0.0)
            return failure(MeanStressCorrectionStatus::MissingMaterialProperty, "Goodman requires a positive ultimate tensile strength.");
        if (mean >= p.ultimateTensileStrength)
            return failure(MeanStressCorrectionStatus::OutsideMaterialStrength, "Mean stress reaches or exceeds ultimate tensile strength.");
        denominator = 1.0 - mean / p.ultimateTensileStrength;
        r.correctedAmplitude = amplitude / denominator;
        break;
    case MeanStressCorrectionKind::Gerber: {
        if (!std::isfinite(p.ultimateTensileStrength) || p.ultimateTensileStrength <= 0.0)
            return failure(MeanStressCorrectionStatus::MissingMaterialProperty, "Gerber requires a positive ultimate tensile strength.");
        const double ratio = mean / p.ultimateTensileStrength;
        denominator = 1.0 - ratio * ratio;
        r.correctedAmplitude = amplitude / denominator;
        break;
    }
    case MeanStressCorrectionKind::Soderberg:
        if (!std::isfinite(p.yieldStrength) || p.yieldStrength <= 0.0)
            return failure(MeanStressCorrectionStatus::MissingMaterialProperty, "Soderberg requires a positive yield strength.");
        if (mean >= p.yieldStrength)
            return failure(MeanStressCorrectionStatus::OutsideMaterialStrength, "Mean stress reaches or exceeds yield strength.");
        denominator = 1.0 - mean / p.yieldStrength;
        r.correctedAmplitude = amplitude / denominator;
        break;
    case MeanStressCorrectionKind::Morrow:
        if (!std::isfinite(p.trueFractureStrength) || p.trueFractureStrength <= 0.0)
            return failure(MeanStressCorrectionStatus::MissingMaterialProperty, "Morrow requires a positive true fracture strength.");
        if (mean >= p.trueFractureStrength)
            return failure(MeanStressCorrectionStatus::OutsideMaterialStrength, "Mean stress reaches or exceeds true fracture strength.");
        denominator = 1.0 - mean / p.trueFractureStrength;
        r.correctedAmplitude = amplitude / denominator;
        break;
    case MeanStressCorrectionKind::SmithWatsonTopper:
        if (r.maximumStress <= 0.0)
            return failure(MeanStressCorrectionStatus::NonTensileMaximumStress, "Stress-based SWT requires a positive maximum cycle stress.");
        r.correctedAmplitude = std::sqrt(amplitude * r.maximumStress);
        break;
    }
    if (kind != MeanStressCorrectionKind::None &&
        kind != MeanStressCorrectionKind::SmithWatsonTopper &&
        (!std::isfinite(denominator) || denominator <= 0.0))
        return failure(MeanStressCorrectionStatus::NonPositiveDenominator, "Mean-stress correction denominator is not positive.");
    if (!std::isfinite(r.correctedAmplitude) || r.correctedAmplitude < 0.0)
        return failure(MeanStressCorrectionStatus::InvalidInput, "Corrected stress amplitude is not finite.");
    r.status = MeanStressCorrectionStatus::Success;
    r.diagnostic = "Mean-stress correction evaluated using the supplied uniaxial cycle values.";
    return r;
}

} // namespace Fatigue
} // namespace OpenModel
