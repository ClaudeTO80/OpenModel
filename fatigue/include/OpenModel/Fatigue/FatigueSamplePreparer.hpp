#pragma once

#include <OpenModel/Fatigue/TensorNormalizer.hpp>

namespace OpenModel {
namespace Fatigue {

enum class FatiguePreparationStatus {
    Success,
    SourceReadFailed,
    UnsupportedQuantity,
    IncompatiblePhysicalDimension,
    AveragedSampleRejected,
    InterpolatedSampleRejected,
    ExtrapolatedSampleRejected,
    NormalizationFailed
};

/** Policies for provenance that may make a result unsuitable for fatigue. */
struct FatiguePreparationOptions {
    bool allowAveraged = false;
    bool allowDerived = true;
    bool allowInterpolated = false;
    bool allowExtrapolated = false;
};

/** A stress/strain sample ready for fatigue calculations, retaining its raw origin. */
struct PreparedTensorSample {
    FatiguePreparationStatus status = FatiguePreparationStatus::SourceReadFailed;
    ResultSample raw;
    Tensor3 tensor;                         // canonical Cartesian tensor in targetFrame
    std::array<double, 3> principalValues{{0.0, 0.0, 0.0}};
    bool principalValuesAvailable = false;
    double vonMisesStress = 0.0;             // meaningful only for Stress
    bool vonMisesAvailable = false;
    ReferenceFrame targetFrame = ReferenceFrame::Global;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatiguePreparationStatus::Success; }
};

/** A vector sample ready for downstream fatigue kinematics. */
struct PreparedVectorSample {
    FatiguePreparationStatus status = FatiguePreparationStatus::SourceReadFailed;
    ResultSample raw;
    Vector3 vector;
    ReferenceFrame targetFrame = ReferenceFrame::Global;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatiguePreparationStatus::Success; }
};

/**
 * Reads one exact source sample and prepares it for solver-independent fatigue
 * calculations. This layer owns policy checks; the source adapter remains raw.
 */
class FatigueSamplePreparer final {
public:
    static PreparedTensorSample prepareTensor(
        const IResultSource& source,
        const std::string& fieldName,
        const ResultPosition& position,
        std::size_t stepIndex,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& options = FatiguePreparationOptions());

    static PreparedVectorSample prepareVector(
        const IResultSource& source,
        const std::string& fieldName,
        const ResultPosition& position,
        std::size_t stepIndex,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& options = FatiguePreparationOptions());
};

} // namespace Fatigue
} // namespace OpenModel
