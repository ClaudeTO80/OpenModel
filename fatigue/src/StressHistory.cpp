#include <OpenModel/Fatigue/StressHistory.hpp>

#include <cmath>

namespace OpenModel {
namespace Fatigue {
namespace {

StressHistoryBuildResult failure(StressHistoryStatus status, const std::string& diagnostic) {
    StressHistoryBuildResult result;
    result.status = status;
    result.diagnostic = diagnostic;
    return result;
}

bool sameDimension(const PhysicalDimension& a, const PhysicalDimension& b) {
    return a.known == b.known && (!a.known ||
        (a.length == b.length && a.mass == b.mass && a.time == b.time &&
         a.current == b.current && a.temperature == b.temperature &&
         a.amount == b.amount && a.luminousIntensity == b.luminousIntensity));
}

bool sameUnit(const PhysicalUnit& a, const PhysicalUnit& b) {
    if (!sameDimension(a.dimension, b.dimension) || a.symbol != b.symbol)
        return false;
    return a.scaleToSI == b.scaleToSI && a.offsetToSI == b.offsetToSI;
}

} // namespace

StressHistoryBuildResult StressHistoryBuilder::build(
    const IResultSource& source, const std::string& fieldName,
    const ResultPosition& position, ReferenceFrame targetFrame,
    const FatiguePreparationOptions& options) {
    const std::size_t count = source.stepCount();
    if (count == 0)
        return failure(StressHistoryStatus::EmptySource, "The result source contains no steps.");

    StressHistory history;
    history.position_ = position;
    history.frame_ = targetFrame;

    double previousTime = 0.0;
    bool havePreviousTime = false;

    for (std::size_t i = 0; i < count; ++i) {
        ResultStep step;
        if (!source.getStep(i, step))
            return failure(StressHistoryStatus::StepUnavailable,
                           "The result source could not provide step " + std::to_string(i) + ".");

        if (step.parameterKind != StepParameterKind::Time)
            return failure(StressHistoryStatus::NonTimeStep,
                           "Step " + std::to_string(i) +
                           " is not explicitly time-valued; load factors and frequencies are not physical time.");

        if (!std::isfinite(step.parameter))
            return failure(StressHistoryStatus::InvalidTime,
                           "Step " + std::to_string(i) + " has a non-finite time value.");

        if (havePreviousTime && !(step.parameter > previousTime))
            return failure(StressHistoryStatus::NonIncreasingTime,
                           "Physical times must be strictly increasing; duplicate or reordered times are not accepted.");

        auto prepared = FatigueSamplePreparer::prepareTensor(
            source, fieldName, position, i, targetFrame, options);
        if (!prepared)
            return failure(StressHistoryStatus::SamplePreparationFailed,
                           "Could not prepare tensor sample at step " + std::to_string(i) +
                           ": " + prepared.diagnostic);

        if (i == 0) {
            history.quantity_ = prepared.raw.quantity;
            history.unit_ = prepared.raw.unit;
        } else {
            if (prepared.raw.quantity != history.quantity_)
                return failure(StressHistoryStatus::MixedQuantity,
                               "A single history cannot mix stress and strain quantities.");
            if (!sameUnit(prepared.raw.unit, history.unit_))
                return failure(StressHistoryStatus::InconsistentUnits,
                               "All samples in a history must use identical declared units; convert explicitly upstream.");
        }

        StressHistorySample sample;
        sample.time = step.parameter;
        sample.sourceStepIndex = i;
        sample.sourceStep = step;
        sample.prepared = prepared;
        history.samples_.push_back(sample);

        previousTime = step.parameter;
        havePreviousTime = true;
    }

    StressHistoryBuildResult result;
    result.status = StressHistoryStatus::Success;
    result.history = history;
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
