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

/**
 * One ordered segment of an operating spectrum. Each block is counted
 * independently, so its time origin may restart at zero. repetitions is the
 * number of occurrences of this block in one complete spectrum repetition.
 */
struct FatigueLoadBlockInput {
    std::string label;
    const IResultSource* source = nullptr;
    std::string fieldName;
    ResultPosition position;
    StressProjection projection;
    double repetitions = 1.0;
};

/** Traceable result for one spectrum block, in the original input order. */
struct FatigueLoadBlockResult {
    std::string label;
    double repetitions = 1.0;
    double damagePerBlock = 0.0;
    double damageContribution = 0.0;
    FatigueAnalysisResult analysis;
};

enum class FatigueSpectrumStatus {
    Success,
    EmptySpectrum,
    InvalidBlock,
    BlockAnalysisFailed,
    NumericalOverflow
};

/**
 * Total damage caused by one complete ordered spectrum. spectrumLife is the
 * number of repetitions of that complete spectrum to reach Miner D=1.
 */
struct FatigueSpectrumResult {
    FatigueSpectrumStatus status = FatigueSpectrumStatus::EmptySpectrum;
    std::vector<FatigueLoadBlockResult> blocks;
    std::size_t failedBlockIndex = 0;
    double totalDamage = 0.0;
    double spectrumLife = 0.0;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatigueSpectrumStatus::Success; }
};

/**
 * Executes multiple independent time histories as an ordered load spectrum.
 * Damage is accumulated linearly in the supplied block order; no synthetic
 * concatenation of histories or implicit time reset is performed.
 */
class FatigueSpectrumAnalysis final {
public:
    static FatigueSpectrumResult run(
        const std::vector<FatigueLoadBlockInput>& blocks,
        const FatigueMaterial& material,
        double temperatureKelvin,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& preparationOptions = FatiguePreparationOptions());
};

} // namespace Fatigue
} // namespace OpenModel
