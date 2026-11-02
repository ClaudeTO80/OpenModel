#pragma once

#include <OpenModel/Fatigue/FatigueSamplePreparer.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

/** A normalized tensor sample at one physical time and one FEM calculation point. */
struct StressHistorySample {
    double time = 0.0;                 // Physical time in the field's declared time unit.
    std::size_t sourceStepIndex = 0;    // Index into the source; never interpreted as time.
    ResultStep sourceStep;
    PreparedTensorSample prepared;    // Full tensor and original solver sample/provenance.
};

/** A complete, ordered stress OR strain history at one stable FEM result position. */
class StressHistory final {
public:
    const ResultPosition& position() const noexcept { return position_; }
    QuantityKind quantity() const noexcept { return quantity_; }
    const PhysicalUnit& unit() const noexcept { return unit_; }
    ReferenceFrame frame() const noexcept { return frame_; }
    const std::vector<StressHistorySample>& samples() const noexcept { return samples_; }
    bool empty() const noexcept { return samples_.empty(); }
    std::size_t size() const noexcept { return samples_.size(); }

private:
    friend struct StressHistoryBuildResult;
    friend class StressHistoryBuilder;
    ResultPosition position_;
    QuantityKind quantity_ = QuantityKind::Unknown;
    PhysicalUnit unit_;
    ReferenceFrame frame_ = ReferenceFrame::Global;
    std::vector<StressHistorySample> samples_;
};

enum class StressHistoryStatus {
    Success,
    EmptySource,
    StepUnavailable,
    NonTimeStep,
    InvalidTime,
    NonIncreasingTime,
    SamplePreparationFailed,
    MixedQuantity,
    InconsistentUnits
};

struct StressHistoryBuildResult {
    StressHistoryStatus status = StressHistoryStatus::EmptySource;
    StressHistory history;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == StressHistoryStatus::Success; }
};

/**
 * Builds a history by reading the exact same field and FEM position at every
 * source step. Only explicitly time-valued steps are accepted: load factors,
 * frequency values and step indices are never silently treated as time.
 */
class StressHistoryBuilder final {
public:
    static StressHistoryBuildResult build(
        const IResultSource& source,
        const std::string& fieldName,
        const ResultPosition& position,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& options = FatiguePreparationOptions());
};

} // namespace Fatigue
} // namespace OpenModel
