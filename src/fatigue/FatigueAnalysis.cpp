#include <OpenModel/Fatigue/FatigueAnalysis.hpp>

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

} // namespace Fatigue
} // namespace OpenModel
