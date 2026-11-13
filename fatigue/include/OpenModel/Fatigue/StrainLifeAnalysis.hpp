#pragma once

#include <OpenModel/Fatigue/LocalStrainCorrection.hpp>
#include <OpenModel/Fatigue/StressHistory.hpp>

namespace OpenModel {
namespace Fatigue {

enum class StrainLifeAnalysisStatus {
    Success,
    InvalidInput,
    HistoryBuildFailed,
    NonStressHistory,
    CycleCountingFailed,
    LocalCorrectionFailed,
    DamageEvaluationFailed
};

/** Full, auditable outcome of a uniaxial FEM strain-life analysis. */
struct StrainLifeAnalysisResult {
    StrainLifeAnalysisStatus status = StrainLifeAnalysisStatus::InvalidInput;
    std::string fieldName;
    ResultPosition position;
    ResultSourceMetadata source;
    StressHistory history;
    CycleCountingResult cycleCounting;
    LocalStrainCorrectionResult localCorrection;
    FatigueDamageResult damage;
    StressProjection projection;
    LocalStrainCorrectionMethod correctionMethod = LocalStrainCorrectionMethod::Neuber;
    StrainLifeMeanStressModel meanStressModel = StrainLifeMeanStressModel::None;
    double temperatureKelvin = 293.15;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == StrainLifeAnalysisStatus::Success; }
};

/**
 * Complete uniaxial FEM-to-strain-life pipeline:
 * tensor history -> explicit directional projection -> Rainflow ->
 * Neuber/Glinka local strain conversion -> CMB/Morrow/SWT Miner damage.
 *
 * The cyclic Ramberg-Osgood and strain-life material records must use exactly
 * matching stress/strain units and Young's modulus. No unit conversion or
 * loading-direction inference is performed.
 */
class StrainLifeAnalysis final {
public:
    static StrainLifeAnalysisResult run(
        const IResultSource& source,
        const std::string& fieldName,
        const ResultPosition& position,
        const StressProjection& projection,
        const CyclicStressStrainParameters& cyclicMaterial,
        const StrainLifeMaterialParameters& strainLifeMaterial,
        StrainLifeMeanStressModel meanStressModel = StrainLifeMeanStressModel::None,
        LocalStrainCorrectionMethod correctionMethod = LocalStrainCorrectionMethod::Neuber,
        double temperatureKelvin = 293.15,
        ReferenceFrame targetFrame = ReferenceFrame::Global,
        const FatiguePreparationOptions& preparationOptions = FatiguePreparationOptions(),
        double minimumReversals = 1.0,
        double maximumReversals = 1.0e16);
};

} // namespace Fatigue
} // namespace OpenModel
