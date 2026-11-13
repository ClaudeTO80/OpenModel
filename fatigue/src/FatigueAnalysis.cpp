#include <OpenModel/Fatigue/FatigueAnalysis.hpp>

#include <cmath>
#include <limits>

namespace OpenModel {
namespace Fatigue {

FatigueAnalysisResult FatigueAnalysis::run(
    const IResultSource& source,
    const std::string& fieldName,
    const ResultPosition& position,
    const FatigueMaterial& material,
    const StressProjection& projection,
    double temperatureKelvin,
    ReferenceFrame targetFrame,
    const FatiguePreparationOptions& preparationOptions) {
    FatigueAnalysisResult result;
    result.fieldName = fieldName;
    result.position = position;
    result.source = source.metadata();
    result.projection = projection;
    result.temperatureKelvin = temperatureKelvin;

    std::string materialDiagnostic;
    if (material.validate(materialDiagnostic) != FatigueCurveStatus::Success) {
        result.status = FatigueAnalysisStatus::InvalidMaterial;
        result.diagnostic = "Invalid fatigue material: " + materialDiagnostic;
        return result;
    }
    const auto* curve = material.findCurve(FatigueCurveKind::StressLifeSN);
    if (!curve) {
        result.status = FatigueAnalysisStatus::MissingSNcurve;
        result.diagnostic = "The selected material has no S-N curve.";
        return result;
    }

    auto history = StressHistoryBuilder::build(
        source, fieldName, position, targetFrame, preparationOptions);
    if (!history) {
        result.status = FatigueAnalysisStatus::HistoryBuildFailed;
        result.diagnostic = history.diagnostic;
        return result;
    }
    result.history = history.history;
    if (result.history.quantity() != QuantityKind::Stress) {
        result.status = FatigueAnalysisStatus::NonStressHistory;
        result.diagnostic = "The S-N Palmgren-Miner workflow requires a stress history.";
        return result;
    }

    result.cycleCounting = CycleCounting::rainflow(result.history, projection);
    if (!result.cycleCounting) {
        result.status = FatigueAnalysisStatus::CycleCountingFailed;
        result.diagnostic = result.cycleCounting.diagnostic;
        return result;
    }

    FatigueDamageInput damageInput;
    damageInput.cycles = &result.cycleCounting.cycles;
    damageInput.curve = curve;
    damageInput.amplitudeUnit = result.history.unit();
    damageInput.temperatureKelvin = temperatureKelvin;

    PalmgrenMiner miner;
    result.damage = miner.evaluate(damageInput);
    if (!result.damage) {
        result.status = FatigueAnalysisStatus::DamageEvaluationFailed;
        result.diagnostic = result.damage.diagnostic;
        return result;
    }

    result.status = FatigueAnalysisStatus::Success;
    result.diagnostic = "Fatigue analysis completed. " + result.damage.diagnostic;
    return result;
}

FatigueSpectrumResult FatigueSpectrumAnalysis::run(
    const std::vector<FatigueLoadBlockInput>& blocks,
    const FatigueMaterial& material,
    double temperatureKelvin,
    ReferenceFrame targetFrame,
    const FatiguePreparationOptions& preparationOptions) {
    FatigueSpectrumResult result;
    if (blocks.empty()) {
        result.status = FatigueSpectrumStatus::EmptySpectrum;
        result.diagnostic = "At least one load block is required.";
        return result;
    }

    result.blocks.reserve(blocks.size());
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        const auto& input = blocks[i];
        if (!input.source || input.fieldName.empty() ||
            !std::isfinite(input.repetitions) || input.repetitions <= 0.0) {
            result.status = FatigueSpectrumStatus::InvalidBlock;
            result.failedBlockIndex = i;
            result.diagnostic = "Load block " + std::to_string(i) +
                " has a null source, empty field name or invalid repetition count.";
            return result;
        }

        FatigueLoadBlockResult blockResult;
        blockResult.label = input.label;
        blockResult.repetitions = input.repetitions;
        blockResult.analysis = FatigueAnalysis::run(
            *input.source, input.fieldName, input.position, material,
            input.projection, temperatureKelvin, targetFrame, preparationOptions);
        if (!blockResult.analysis) {
            result.status = FatigueSpectrumStatus::BlockAnalysisFailed;
            result.failedBlockIndex = i;
            result.diagnostic = "Load block " + std::to_string(i) +
                (input.label.empty() ? std::string() : " (" + input.label + ")") +
                " failed: " + blockResult.analysis.diagnostic;
            result.blocks.push_back(blockResult);
            return result;
        }

        blockResult.damagePerBlock = blockResult.analysis.damage.damage;
        blockResult.damageContribution = blockResult.damagePerBlock * input.repetitions;
        if (!std::isfinite(blockResult.damageContribution) ||
            blockResult.damageContribution > std::numeric_limits<double>::max() - result.totalDamage) {
            result.status = FatigueSpectrumStatus::NumericalOverflow;
            result.failedBlockIndex = i;
            result.diagnostic = "Damage accumulation overflowed at load block " + std::to_string(i) + ".";
            result.blocks.push_back(blockResult);
            return result;
        }

        result.totalDamage += blockResult.damageContribution;
        result.blocks.push_back(blockResult);
    }

    result.spectrumLife = result.totalDamage > 0.0
        ? 1.0 / result.totalDamage
        : std::numeric_limits<double>::infinity();
    result.status = FatigueSpectrumStatus::Success;
    result.diagnostic = "All load blocks were evaluated in input order using linear Palmgren-Miner accumulation.";
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
