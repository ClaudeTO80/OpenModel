#include <OpenModel/Fatigue/FatigueSpectrumReport.hpp>

namespace OpenModel {
namespace Fatigue {

FatigueSpectrumReport FatigueSpectrumReportBuilder::build(
    const std::vector<FatigueSpectrumPositionInput>& positions,
    double temperatureKelvin,
    ReferenceFrame targetFrame,
    const FatiguePreparationOptions& preparationOptions) {
    FatigueSpectrumReport report;
    if (positions.empty()) {
        report.status = FatigueSpectrumReportStatus::EmptySelection;
        report.diagnostic = "No FEM result positions were selected.";
        return report;
    }

    report.positions.reserve(positions.size());
    for (std::size_t i = 0; i < positions.size(); ++i) {
        const auto& input = positions[i];
        FatigueSpectrumPositionResult row;
        row.position = input.position;
        row.label = input.label;

        if (!input.material || input.blocks.empty()) {
            row.spectrum.status = input.blocks.empty()
                ? FatigueSpectrumStatus::EmptySpectrum
                : FatigueSpectrumStatus::InvalidBlock;
            row.spectrum.diagnostic = !input.material
                ? "No fatigue material was assigned to this FEM position."
                : "No load blocks were supplied for this FEM position.";
        } else {
            auto blocks = input.blocks;
            for (auto& block : blocks) block.position = input.position;
            row.spectrum = FatigueSpectrumAnalysis::run(
                blocks, *input.material, temperatureKelvin,
                targetFrame, preparationOptions);
        }

        row.hasDamage = static_cast<bool>(row.spectrum);
        if (row.hasDamage) {
            row.damage = row.spectrum.totalDamage;
            ++report.successfulPositions;
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
        report.status = FatigueSpectrumReportStatus::AllPositionsFailed;
        report.diagnostic = "No selected FEM position has a valid spectrum analysis.";
    } else if (report.failedPositions != 0) {
        report.status = FatigueSpectrumReportStatus::PartialFailure;
        report.diagnostic = "Some FEM positions failed; inspect each spectrum diagnostic.";
    } else {
        report.status = FatigueSpectrumReportStatus::Success;
        report.diagnostic = "All selected FEM positions were evaluated successfully.";
    }
    return report;
}

} // namespace Fatigue
} // namespace OpenModel
