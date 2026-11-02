#include <OpenModel/Fatigue/FatigueSamplePreparer.hpp>

#include <cmath>

namespace OpenModel {
namespace Fatigue {
namespace {

bool dimensionEquals(const PhysicalDimension& actual, int length, int mass, int time) {
    return !actual.known ||
        (actual.length == length && actual.mass == mass && actual.time == time &&
         actual.current == 0 && actual.temperature == 0 &&
         actual.amount == 0 && actual.luminousIntensity == 0);
}

FatiguePreparationStatus checkProvenance(
    const ResultSample& sample, const FatiguePreparationOptions& options, std::string& diagnostic) {
    if (sample.averaged && !options.allowAveraged) {
        diagnostic = "Averaged results are disabled by the fatigue preparation policy.";
        return FatiguePreparationStatus::AveragedSampleRejected;
    }
    if (sample.derived && !options.allowDerived) {
        diagnostic = "Derived results are disabled by the fatigue preparation policy.";
        return FatiguePreparationStatus::UnsupportedQuantity;
    }
    if (sample.interpolated && !options.allowInterpolated) {
        diagnostic = "Interpolated results are disabled by the fatigue preparation policy.";
        return FatiguePreparationStatus::InterpolatedSampleRejected;
    }
    if (sample.extrapolated && !options.allowExtrapolated) {
        diagnostic = "Extrapolated results are disabled by the fatigue preparation policy.";
        return FatiguePreparationStatus::ExtrapolatedSampleRejected;
    }
    return FatiguePreparationStatus::Success;
}

PreparedTensorSample tensorFailure(
    FatiguePreparationStatus status, const ResultSample& raw, const std::string& diagnostic,
    ReferenceFrame target) {
    PreparedTensorSample result;
    result.status = status;
    result.raw = raw;
    result.diagnostic = diagnostic;
    result.targetFrame = target;
    return result;
}

PreparedVectorSample vectorFailure(
    FatiguePreparationStatus status, const ResultSample& raw, const std::string& diagnostic,
    ReferenceFrame target) {
    PreparedVectorSample result;
    result.status = status;
    result.raw = raw;
    result.diagnostic = diagnostic;
    result.targetFrame = target;
    return result;
}

} // namespace

PreparedTensorSample FatigueSamplePreparer::prepareTensor(
    const IResultSource& source, const std::string& fieldName,
    const ResultPosition& position, std::size_t stepIndex,
    ReferenceFrame targetFrame, const FatiguePreparationOptions& options) {
    ResultSample raw;
    if (!source.readSample(fieldName, position, stepIndex, raw))
        return tensorFailure(FatiguePreparationStatus::SourceReadFailed, raw,
                             "The requested field, position or step is missing, ambiguous or invalid.",
                             targetFrame);

    std::string diagnostic;
    const auto provenance = checkProvenance(raw, options, diagnostic);
    if (provenance != FatiguePreparationStatus::Success)
        return tensorFailure(provenance, raw, diagnostic, targetFrame);

    if (raw.quantity != QuantityKind::Stress && raw.quantity != QuantityKind::Strain)
        return tensorFailure(FatiguePreparationStatus::UnsupportedQuantity, raw,
                             "Fatigue tensor preparation accepts only stress or strain fields.",
                             targetFrame);

    const bool dimensionValid = raw.quantity == QuantityKind::Stress
        ? dimensionEquals(raw.unit.dimension, -1, 1, -2)
        : dimensionEquals(raw.unit.dimension, 0, 0, 0);
    if (!dimensionValid)
        return tensorFailure(FatiguePreparationStatus::IncompatiblePhysicalDimension, raw,
                             "The declared physical dimension is incompatible with the field quantity.",
                             targetFrame);

    const auto normalized = TensorNormalizer::normalizeTensor(raw, targetFrame);
    if (!normalized)
        return tensorFailure(FatiguePreparationStatus::NormalizationFailed, raw,
                             normalized.diagnostic, targetFrame);

    PreparedTensorSample result;
    result.status = FatiguePreparationStatus::Success;
    result.raw = raw;
    result.tensor = normalized.tensor;
    result.targetFrame = targetFrame;
    result.principalValuesAvailable =
        TensorNormalizer::principalValues(result.tensor, result.principalValues);
    if (raw.quantity == QuantityKind::Stress) {
        result.vonMisesStress = TensorNormalizer::vonMisesStress(result.tensor);
        result.vonMisesAvailable = std::isfinite(result.vonMisesStress);
    }
    if (!result.principalValuesAvailable ||
        (raw.quantity == QuantityKind::Stress && !result.vonMisesAvailable)) {
        return tensorFailure(FatiguePreparationStatus::NormalizationFailed, raw,
                             "Normalized tensor produced non-finite derived values.", targetFrame);
    }
    return result;
}

PreparedVectorSample FatigueSamplePreparer::prepareVector(
    const IResultSource& source, const std::string& fieldName,
    const ResultPosition& position, std::size_t stepIndex,
    ReferenceFrame targetFrame, const FatiguePreparationOptions& options) {
    ResultSample raw;
    if (!source.readSample(fieldName, position, stepIndex, raw))
        return vectorFailure(FatiguePreparationStatus::SourceReadFailed, raw,
                             "The requested field, position or step is missing, ambiguous or invalid.",
                             targetFrame);

    std::string diagnostic;
    const auto provenance = checkProvenance(raw, options, diagnostic);
    if (provenance != FatiguePreparationStatus::Success)
        return vectorFailure(provenance, raw, diagnostic, targetFrame);

    if (raw.quantity != QuantityKind::Displacement &&
        raw.quantity != QuantityKind::Velocity &&
        raw.quantity != QuantityKind::Acceleration &&
        raw.quantity != QuantityKind::Force &&
        raw.quantity != QuantityKind::ReactionForce)
        return vectorFailure(FatiguePreparationStatus::UnsupportedQuantity, raw,
                             "The requested vector quantity is not supported by fatigue preparation.",
                             targetFrame);

    const auto normalized = TensorNormalizer::normalizeVector(raw, targetFrame);
    if (!normalized)
        return vectorFailure(FatiguePreparationStatus::NormalizationFailed, raw,
                             normalized.diagnostic, targetFrame);

    PreparedVectorSample result;
    result.status = FatiguePreparationStatus::Success;
    result.raw = raw;
    result.vector = normalized.vector;
    result.targetFrame = targetFrame;
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
