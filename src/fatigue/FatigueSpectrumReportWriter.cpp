#include <OpenModel/Fatigue/FatigueReportWriter.hpp>

#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace OpenModel {
namespace Fatigue {
namespace {
const char* reportStatusName(FatigueSpectrumReportStatus s) {
    switch (s) {
    case FatigueSpectrumReportStatus::Success: return "success";
    case FatigueSpectrumReportStatus::EmptySelection: return "empty_selection";
    case FatigueSpectrumReportStatus::InvalidInput: return "invalid_input";
    case FatigueSpectrumReportStatus::PartialFailure: return "partial_failure";
    case FatigueSpectrumReportStatus::AllPositionsFailed: return "all_positions_failed";
    }
    return "unknown";
}
const char* spectrumStatusName(FatigueSpectrumStatus s) {
    switch (s) {
    case FatigueSpectrumStatus::Success: return "success";
    case FatigueSpectrumStatus::EmptySpectrum: return "empty_spectrum";
    case FatigueSpectrumStatus::InvalidBlock: return "invalid_block";
    case FatigueSpectrumStatus::BlockAnalysisFailed: return "block_analysis_failed";
    case FatigueSpectrumStatus::NumericalOverflow: return "numerical_overflow";
    }
    return "unknown";
}
const char* analysisStatusName(FatigueAnalysisStatus s) {
    switch (s) {
    case FatigueAnalysisStatus::Success: return "success";
    case FatigueAnalysisStatus::InvalidMaterial: return "invalid_material";
    case FatigueAnalysisStatus::MissingSNcurve: return "missing_sn_curve";
    case FatigueAnalysisStatus::HistoryBuildFailed: return "history_build_failed";
    case FatigueAnalysisStatus::NonStressHistory: return "non_stress_history";
    case FatigueAnalysisStatus::CycleCountingFailed: return "cycle_counting_failed";
    case FatigueAnalysisStatus::DamageEvaluationFailed: return "damage_evaluation_failed";
    }
    return "unknown";
}
const char* positionName(ResultLocation l) {
    switch (l) {
    case ResultLocation::Node: return "node";
    case ResultLocation::Element: return "element";
    case ResultLocation::IntegrationPoint: return "integration_point";
    }
    return "unknown";
}
std::string csvQuote(const std::string& s) {
    std::string out = "\\\"";
    for (char c : s) { out += c; if (c == '"') out += '"'; }
    return out + '"';
}
std::string jsonQuote(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': o << "\\\\""; break;
        case '\\\\': o << "\\\\\\\\"; break;
        case '\\b': o << "\\\\b"; break;
        case '\\f': o << "\\\\f"; break;
        case '\\n': o << "\\\\n"; break;
        case '\\r': o << "\\\\r"; break;
        case '\\t': o << "\\\\t"; break;
        default:
            if (c < 0x20) o << "\\\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else o << static_cast<char>(c);
        }
    }
    return o.str() + '"';
}
void number(std::ostream& o, double v) {
    if (std::isfinite(v)) o << std::setprecision(17) << v;
    else o << "null";
}
void position(std::ostream& o, const ResultPosition& p) {
    o << "{\\"location\\":" << jsonQuote(positionName(p.location))
      << ",\\"entity_id\\":" << p.entityId
      << ",\\"integration_point\\":" << p.integrationPoint
      << ",\\"section_point\\":" << p.sectionPoint << '}';
}
FatigueReportWriteResult invalid() {
    return {FatigueReportWriteStatus::InvalidReport, "Spectrum report has no serializable selection."};
}
}

FatigueReportWriteResult FatigueReportWriter::writeCsvSpectrumSummary(
    const FatigueSpectrumReport& report, std::ostream& output) {
    if (report.status == FatigueSpectrumReportStatus::EmptySelection ||
        report.status == FatigueSpectrumReportStatus::InvalidInput) return invalid();
    output << "report_status,location,entity_id,integration_point,section_point,label,spectrum_status,has_damage,damage,spectrum_life,block_count,diagnostic\\n";
    for (const auto& row : report.positions) {
        output << reportStatusName(report.status) << ',' << positionName(row.position.location) << ','
               << row.position.entityId << ',' << row.position.integrationPoint << ','
               << row.position.sectionPoint << ',' << csvQuote(row.label) << ','
               << spectrumStatusName(row.spectrum.status) << ','
               << (row.hasDamage ? "true" : "false") << ',';
        if (row.hasDamage) number(output, row.damage);
        output << ',';
        if (row.hasDamage) number(output, row.spectrum.spectrumLife);
        output << ',' << row.spectrum.blocks.size() << ',' << csvQuote(row.spectrum.diagnostic) << '\\n';
    }
    if (!output) return {FatigueReportWriteStatus::StreamError, "Failed while writing spectrum summary CSV."};
    return {FatigueReportWriteStatus::Success, "Spectrum summary CSV written."};
}

FatigueReportWriteResult FatigueReportWriter::writeCsvSpectrumBlocks(
    const FatigueSpectrumReport& report, std::ostream& output) {
    if (report.status == FatigueSpectrumReportStatus::EmptySelection ||
        report.status == FatigueSpectrumReportStatus::InvalidInput) return invalid();
    output << "location,entity_id,integration_point,section_point,position_label,block_index,block_label,repetitions,analysis_status,has_damage,damage_per_block,damage_contribution,cycles_evaluated,diagnostic\\n";
    for (const auto& row : report.positions) {
        for (std::size_t i = 0; i < row.spectrum.blocks.size(); ++i) {
            const auto& block = row.spectrum.blocks[i];
            const bool valid = row.hasDamage && static_cast<bool>(block.analysis);
            output << positionName(row.position.location) << ',' << row.position.entityId << ','
                   << row.position.integrationPoint << ',' << row.position.sectionPoint << ','
                   << csvQuote(row.label) << ',' << i << ',' << csvQuote(block.label) << ',';
            number(output, block.repetitions);
            output << ',' << analysisStatusName(block.analysis.status) << ','
                   << (valid ? "true" : "false") << ',';
            if (valid) number(output, block.damagePerBlock);
            output << ',';
            if (valid) number(output, block.damageContribution);
            output << ',';
            if (valid) output << block.analysis.damage.cyclesEvaluated;
            output << ',' << csvQuote(block.analysis.diagnostic) << '\\n';
        }
    }
    if (!output) return {FatigueReportWriteStatus::StreamError, "Failed while writing spectrum block CSV."};
    return {FatigueReportWriteStatus::Success, "Spectrum block CSV written."};
}

FatigueReportWriteResult FatigueReportWriter::writeSpectrumJson(
    const FatigueSpectrumReport& report, std::ostream& output, bool includeCycles) {
    if (report.status == FatigueSpectrumReportStatus::EmptySelection ||
        report.status == FatigueSpectrumReportStatus::InvalidInput) return invalid();
    output << "{\\"schema\\":\\"openmodel.fatigue-spectrum-report.v1\\",\\"status\\":"
           << jsonQuote(reportStatusName(report.status))
           << ",\\"summary\\":{\\"successful_positions\\":" << report.successfulPositions
           << ",\\"failed_positions\\":" << report.failedPositions
           << ",\\"has_maximum_damage\\":" << (report.hasMaximumDamage ? "true" : "false")
           << ",\\"maximum_damage_index\\":";
    if (report.hasMaximumDamage) output << report.maximumDamageIndex; else output << "null";
    output << ",\\"maximum_damage\\":";
    if (report.hasMaximumDamage) number(output, report.maximumDamage); else output << "null";
    output << "},\\"positions\\":[";
    for (std::size_t i = 0; i < report.positions.size(); ++i) {
        if (i) output << ',';
        const auto& row = report.positions[i];
        output << "{\\"label\\":" << jsonQuote(row.label) << ",\\"position\\":";
        position(output, row.position);
        output << ",\\"spectrum_status\\":" << jsonQuote(spectrumStatusName(row.spectrum.status))
               << ",\\"has_damage\\":" << (row.hasDamage ? "true" : "false")
               << ",\\"damage\\":";
        if (row.hasDamage) number(output, row.damage); else output << "null";
        output << ",\\"spectrum_life\\":";
        if (row.hasDamage) number(output, row.spectrum.spectrumLife); else output << "null";
        output << ",\\"blocks\\":[";
        for (std::size_t j = 0; j < row.spectrum.blocks.size(); ++j) {
            if (j) output << ',';
            const auto& b = row.spectrum.blocks[j];
            const auto& a = b.analysis;
            const bool valid = row.hasDamage && static_cast<bool>(a);
            output << "{\\"label\\":" << jsonQuote(b.label) << ",\\"repetitions\\":";
            number(output, b.repetitions);
            output << ",\\"analysis_status\\":" << jsonQuote(analysisStatusName(a.status))
                   << ",\\"damage_per_block\\":";
            if (valid) number(output, b.damagePerBlock); else output << "null";
            output << ",\\"damage_contribution\\":";
            if (valid) number(output, b.damageContribution); else output << "null";
            output << ",\\"cycles_evaluated\\":";
            if (valid) output << a.damage.cyclesEvaluated; else output << "null";
            output << ",\\"diagnostic\\":" << jsonQuote(a.diagnostic);
            if (includeCycles && valid) {
                output << ",\\"cycles\\":[";
                for (std::size_t k = 0; k < a.cycleCounting.cycles.size(); ++k) {
                    if (k) output << ',';
                    const auto& c = a.cycleCounting.cycles[k];
                    output << "{\\"start_time\\":"; number(output, c.start.time);
                    output << ",\\"end_time\\":"; number(output, c.end.time);
                    output << ",\\"range\\":"; number(output, c.range);
                    output << ",\\"mean\\":"; number(output, c.mean);
                    output << ",\\"count\\":"; number(output, c.count);
                    output << ",\\"start_sample_index\\":" << c.start.historySampleIndex
                           << ",\\"end_sample_index\\":" << c.end.historySampleIndex << '}';
                }
                output << ']';
            }
            output << '}';
        }
        output << "],\\"diagnostic\\":" << jsonQuote(row.spectrum.diagnostic) << '}';
    }
    output << "],\\"diagnostic\\":" << jsonQuote(report.diagnostic) << "}\\n";
    if (!output) return {FatigueReportWriteStatus::StreamError, "Failed while writing spectrum JSON."};
    return {FatigueReportWriteStatus::Success, "Spectrum JSON written."};
}

} // namespace Fatigue
} // namespace OpenModel
