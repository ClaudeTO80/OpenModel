#include <OpenModel/Fatigue/FatigueReport.hpp>

namespace OpenModel {
namespace Fatigue {

FatigueReport FatigueReportBuilder::build(
    const IResultSource& source,
    const std::string& fieldName,
    const std::vector<FatiguePositionInput>& positions,
    double temperatureKelvin,
    ReferenceFrame targetFrame,
    const FatiguePreparationOptions& preparationOptions) {
    FatigueReport report;
    report.fieldName = fieldName;
    report.source = source.metadata();

    if (positions.empty()) {
        report.status = FatigueReportStatus::EmptySelection;
        report.diagnostic = "No FEM result positions were selected.";
        return report;
    }
    if (fieldName.empty()) {
        report.status = FatigueReportStatus::InvalidInput;
        report.diagnostic = "The result field name must not be empty.";
        return report;
    }

    report.positions.reserve(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto& input = positions[i];
        FatiguePositionResult row;
        row.position = input.position;
        row.label = input.label;

        if (!input.material) {
            row.analysis.status = FatigueAnalysisStatus::InvalidMaterial;
            row.analysis.fieldName = fieldName;
            row.analysis.position = input.position;
            row.analysis.source = source.metadata();
            row.analysis.diagnostic = "No material was assigned to this FEM result position.";
        } else {
            row.analysis = FatigueAnalysis::run(
                source, fieldName, input.position, *input.material,
                input.projection, temperatureKelvin, targetFrame, preparationOptions);
        }

        row.hasDamage = static_cast<bool>(row.analysis);
        if (row.hasDamage) {
            row.damage = row.analysis.damage.damage;
            ++report.successfulPositions;
            if (row.damage == 0.0) {
                ++report.zeroDamagePositions;
            } else {
                ++report.positiveDamagePositions;
            }
            if (!report.hasMaximumDamage || row.damage > report.maximumDamage) {
                report.hasMaximumDamage = true;
                report.maximumDamage = row.damage;
                report.maximumDamageIndex = i;
            }
        } else {
            ++report.failedPositions;
        }
        report.positions.push_back(std::move(row));
    }

    if (report.successfulPositions == 0) {
        report.status = FatigueReportStatus::AllPositionsFailed;
        report.diagnostic = "No selected FEM position could be evaluated.";
    } else if (report.failedPositions != 0) {
        report.status = FatigueReportStatus::PartialFailure;
        report.diagnostic = "The report contains successful and failed FEM positions; inspect each row diagnostic.";
    } else {
        report.status = FatigueReportStatus::Success;
        report.diagnostic = "All selected FEM positions were evaluated successfully.";
    }
    return report;
}

} // namespace Fatigue
} // namespace OpenModel
