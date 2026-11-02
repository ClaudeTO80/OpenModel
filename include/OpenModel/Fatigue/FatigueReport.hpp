#pragma once

#include <OpenModel/Fatigue/FatigueAnalysis.hpp>

namespace OpenModel {
namespace Fatigue {

/** Explicit material and projection assignment for one FEM result position. */
struct FatiguePositionInput {
    ResultPosition position;
    const FatigueMaterial* material = nullptr;
    StressProjection projection;
    std::string label;
};

/** One row in a multi-position report. Failed analyses are retained, not dropped. */
struct FatiguePositionResult {
    ResultPosition position;
    std::string label;
    FatigueAnalysisResult analysis;
    bool hasDamage = false;
    double damage = 0.0;
};

enum class FatigueReportStatus {
    Success,
    EmptySelection,
    InvalidInput,
    PartialFailure,
    AllPositionsFailed
};

/**
 * Multi-position report for one result field/source. Material assignment is
 * explicit per position. Nodal, element and native integration-point
 * positions are handled through IResultSource without location fallback.
 */
struct FatigueReport {
    FatigueReportStatus status = FatigueReportStatus::EmptySelection;
    std::string fieldName;
    ResultSourceMetadata source;
    std::vector<FatiguePositionResult> positions;
    std::size_t successfulPositions = 0;
    std::size_t failedPositions = 0;
    std::size_t zeroDamagePositions = 0;
    std::size_t positiveDamagePositions = 0;
    std::size_t maximumDamageIndex = 0;
    bool hasMaximumDamage = false;
    double maximumDamage = 0.0;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatigueReportStatus::Success; }
};

class FatigueReportBuilder final {
public:
    static FatigueReport build(
        const IResultSource& source,
        const std::string& fieldName,
        const std::vector<FatiguePositionInput>& positions,
        double temperatureKelvin,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& preparationOptions = FatiguePreparationOptions());
};

} // namespace Fatigue
} // namespace OpenModel
