#pragma once

#include <OpenModel/Fatigue/FatigueDamageModel.hpp>
#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

namespace OpenModel {
namespace Fatigue {

enum class FatigueAnalysisStatus {
    Success,
    InvalidMaterial,
    MissingSNcurve,
    HistoryBuildFailed,
    NonStressHistory,
    CycleCountingFailed,
    DamageEvaluationFailed
};

/** Complete, traceable outcome for one FEM result position and one load history. */
struct FatigueAnalysisResult {
    FatigueAnalysisStatus status = FatigueAnalysisStatus::HistoryBuildFailed;
    std::string fieldName;
    ResultPosition position;
    ResultSourceMetadata source;
    StressHistory history;
    CycleCountingResult cycleCounting;
    FatigueDamageResult damage;
    StressProjection projection;
    double temperatureKelvin = 293.15;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatigueAnalysisStatus::Success; }
};

/**
 * Orchestrates the solver-independent uniaxial S-N workflow:
 * exact FEM samples -> normalized stress history -> explicit directional
 * projection and Rainflow -> S-N curve -> Palmgren-Miner.
 *
 * The result retains the complete history, cycle endpoints and source
 * metadata. It does not infer material assignment, mean-stress corrections,
 * unit conversions or multiaxial equivalence.
 */
class FatigueAnalysis final {
public:
    static FatigueAnalysisResult run(
        const IResultSource& source,
        const std::string& fieldName,
        const ResultPosition& position,
        const FatigueMaterial& material,
        const StressProjection& projection,
        double temperatureKelvin,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& preparationOptions = FatiguePreparationOptions());
};

} // namespace Fatigue
} // namespace OpenModel
