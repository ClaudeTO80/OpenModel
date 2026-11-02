#pragma once

#include <OpenModel/Fatigue/FatigueReport.hpp>

namespace OpenModel {
namespace Fatigue {

/**
 * One FEM position evaluated against an explicitly defined ordered spectrum.
 * Each block supplies its own source, field, projection and repetitions;
 * the position and material are fixed for the whole spectrum.
 */
struct FatigueSpectrumPositionInput {
    ResultPosition position;
    const FatigueMaterial* material = nullptr;
    std::string label;
    std::vector<FatigueLoadBlockInput> blocks;
};

struct FatigueSpectrumPositionResult {
    ResultPosition position;
    std::string label;
    FatigueSpectrumResult spectrum;
    bool hasDamage = false;
    double damage = 0.0;
};

enum class FatigueSpectrumReportStatus {
    Success,
    EmptySelection,
    InvalidInput,
    PartialFailure,
    AllPositionsFailed
};

/** Multi-position spectrum report. Damage is never aggregated across positions. */
struct FatigueSpectrumReport {
    FatigueSpectrumReportStatus status = FatigueSpectrumReportStatus::EmptySelection;
    std::vector<FatigueSpectrumPositionResult> positions;
    std::size_t successfulPositions = 0;
    std::size_t failedPositions = 0;
    std::size_t maximumDamageIndex = 0;
    bool hasMaximumDamage = false;
    double maximumDamage = 0.0;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatigueSpectrumReportStatus::Success; }
};

class FatigueSpectrumReportBuilder final {
public:
    static FatigueSpectrumReport build(
        const std::vector<FatigueSpectrumPositionInput>& positions,
        double temperatureKelvin,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& preparationOptions = FatiguePreparationOptions());
};

} // namespace Fatigue
} // namespace OpenModel
