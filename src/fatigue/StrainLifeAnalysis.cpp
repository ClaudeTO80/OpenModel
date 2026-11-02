#include <OpenModel/Fatigue/StrainLifeAnalysis.hpp>

#include <cmath>

namespace OpenModel {
namespace Fatigue {
namespace {

bool sameDimension(const PhysicalDimension& a, const PhysicalDimension& b) {
    return a.known == b.known && a.length == b.length && a.mass == b.mass &&
        a.time == b.time && a.current == b.current &&
        a.temperature == b.temperature && a.amount == b.amount &&
        a.luminousIntensity == b.luminousIntensity;
}

bool sameUnit(const PhysicalUnit& a, const PhysicalUnit& b) {
    return sameDimension(a.dimension, b.dimension) && a.symbol == b.symbol &&
        a.scaleToSI == b.scaleToSI && a.offsetToSI == b.offsetToSI;
}

} // namespace

StrainLifeAnalysisResult StrainLifeAnalysis::run(
    const IResultSource& source,
    const std::string& fieldName,
    const ResultPosition& position,
    const StressProjection& projection,
    const CyclicStressStrainParameters& cyclicMaterial,
    const StrainLifeMaterialParameters& strainLifeMaterial,
    StrainLifeMeanStressModel meanStressModel,
    LocalStrainCorrectionMethod correctionMethod,
    double temperatureKelvin,
    ReferenceFrame targetFrame,
    const FatiguePreparationOptions& preparationOptions,
    double minimumReversals,
    double maximumReversals) {
    StrainLifeAnalysisResult result;
    result.fieldName = fieldName;
    result.position = position;
    result.source = source.metadata();
    result.projection = projection;
    result.correctionMethod = correctionMethod;
    result.meanStressModel = meanStressModel;
    result.temperatureKelvin = temperatureKelvin;

    if (fieldName.empty() || !std::isfinite(temperatureKelvin) ||
        temperatureKelvin <= 0.0 ||
        !std::isfinite(minimumReversals) || minimumReversals <= 0.0 ||
        !std::isfinite(maximumReversals) || maximumReversals <= minimumReversals ||
        (meanStressModel != StrainLifeMeanStressModel::None &&
         meanStressModel != StrainLifeMeanStressModel::Morrow &&
         meanStressModel != StrainLifeMeanStressModel::SmithWatsonTopper) ||
        (correctionMethod != LocalStrainCorrectionMethod::Neuber &&
         correctionMethod != LocalStrainCorrectionMethod::Glinka) ||
        !sameUnit(cyclicMaterial.stressUnit, strainLifeMaterial.stressUnit) ||
        !sameUnit(cyclicMaterial.strainUnit, strainLifeMaterial.strainUnit) ||
        cyclicMaterial.youngsModulus != strainLifeMaterial.youngsModulus) {
        result.status = StrainLifeAnalysisStatus::InvalidInput;
        result.diagnostic = "Invalid analysis settings: material units must match exactly and both material records must use the same Young's modulus.";
        return result;
    }

    const auto built = StressHistoryBuilder::build(
        source, fieldName, position, targetFrame, preparationOptions);
    if (!built) {
        result.status = StrainLifeAnalysisStatus::HistoryBuildFailed;
        result.diagnostic = built.diagnostic;
        return result;
    }
    result.history = built.history;
    if (result.history.quantity() != QuantityKind::Stress) {
        result.status = StrainLifeAnalysisStatus::NonStressHistory;
        result.diagnostic = "StrainLifeAnalysis currently requires a stress tensor history; strain-result input is not implicitly reinterpreted.";
        return result;
    }
    if (!sameUnit(result.history.unit(), cyclicMaterial.stressUnit)) {
        result.status = StrainLifeAnalysisStatus::InvalidInput;
        result.diagnostic = "FEM history stress unit must exactly match both material stress units.";
        return result;
    }

    result.cycleCounting = CycleCounting::rainflow(result.history, projection);
    if (!result.cycleCounting) {
        result.status = StrainLifeAnalysisStatus::CycleCountingFailed;
        result.diagnostic = result.cycleCounting.diagnostic;
        return result;
    }

    result.localCorrection = LocalStrainCorrection::convert(
        result.cycleCounting.cycles, result.history.unit(), cyclicMaterial, correctionMethod);
    if (!result.localCorrection) {
        result.status = StrainLifeAnalysisStatus::LocalCorrectionFailed;
        result.diagnostic = result.localCorrection.diagnostic;
        return result;
    }

    StrainLifeDamageInput damageInput;
    damageInput.cycles = &result.localCorrection.cycles;
    damageInput.material = &strainLifeMaterial;
    damageInput.stressUnit = result.history.unit();
    damageInput.strainUnit = cyclicMaterial.strainUnit;
    damageInput.meanStressModel = meanStressModel;
    damageInput.minimumReversals = minimumReversals;
    damageInput.maximumReversals = maximumReversals;

    StrainLifeDamageModel damageModel;
    result.damage = damageModel.evaluate(damageInput);
    if (!result.damage) {
        result.status = StrainLifeAnalysisStatus::DamageEvaluationFailed;
        result.diagnostic = result.damage.diagnostic;
        return result;
    }

    result.status = StrainLifeAnalysisStatus::Success;
    result.diagnostic = "Uniaxial strain-life analysis completed using explicit tensor projection, Rainflow, local strain correction and strain-life Miner damage. Mean stress remains the elastic cycle mean in the local correction approximation.";
    return result;
}

} // namespace Fatigue
} // namespace OpenModel
