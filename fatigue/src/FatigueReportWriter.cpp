#include <OpenModel/Fatigue/FatigueReportWriter.hpp>

#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace OpenModel {
namespace Fatigue {
namespace {

const char* reportStatus(FatigueReportStatus s) {
    switch (s) {
    case FatigueReportStatus::Success: return "success";
    case FatigueReportStatus::EmptySelection: return "empty_selection";
    case FatigueReportStatus::InvalidInput: return "invalid_input";
    case FatigueReportStatus::PartialFailure: return "partial_failure";
    case FatigueReportStatus::AllPositionsFailed: return "all_positions_failed";
    }
    return "unknown";
}
const char* analysisStatus(FatigueAnalysisStatus s) {
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
const char* locationName(ResultLocation l) {
    switch (l) {
    case ResultLocation::Node: return "node";
    case ResultLocation::Element: return "element";
    case ResultLocation::IntegrationPoint: return "integration_point";
    }
    return "unknown";
}
std::string csv(const std::string& s) {
    std::string out="\"";
    for (char c : s) { if (c=='\"') out += "\""; else out += c; }
    return out + "\"";
}
std::string json(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\b': o << "\\b"; break;
        case '\f': o << "\\f"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default:
            if (c < 0x20) o << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c) << std::dec;
            else o << static_cast<char>(c);
        }
    }
    o << '"';
    return o.str();
}
void writePosition(std::ostream& o, const ResultPosition& p) {
    o << "{\"location\":" << json(locationName(p.location))
      << ",\"entity_id\":" << p.entityId
      << ",\"integration_point\":" << p.integrationPoint
      << ",\"section_point\":" << p.sectionPoint << '}';
}
void writeNumber(std::ostream& o, double v) {
    if (std::isfinite(v)) o << std::setprecision(17) << v;
    else o << "null";
}
FatigueReportWriteResult failure(FatigueReportWriteStatus s, const char* d) {
    return {s, d};
}
bool validReport(const FatigueReport& r) {
    return r.status != FatigueReportStatus::EmptySelection &&
           r.status != FatigueReportStatus::InvalidInput;
}
}

FatigueReportWriteResult FatigueReportWriter::writeCsvSummary(
    const FatigueReport& report, std::ostream& output) {
    if (!validReport(report)) return failure(FatigueReportWriteStatus::InvalidReport, "Report has no serializable selection.");
    output << "report_status,field,solver,location,entity_id,integration_point,section_point,label,analysis_status,has_damage,damage,block_life,cycles_evaluated,temperature_kelvin,diagnostic\n";
    for (const auto& row : report.positions) {
        const auto& a = row.analysis;
        output << csv(reportStatus(report.status)) << ',' << csv(report.fieldName) << ','
               << csv(report.source.solverName) << ',' << locationName(row.position.location) << ','
               << row.position.entityId << ',' << row.position.integrationPoint << ','
               << row.position.sectionPoint << ',' << csv(row.label) << ','
               << analysisStatus(a.status) << ',' << (row.hasDamage ? "true" : "false") << ',';
        if (row.hasDamage) writeNumber(output, row.damage); else output << "";
        output << ',';
        if (row.hasDamage) writeNumber(output, a.damage.blockLife); else output << "";
        output << ',';
        if (row.hasDamage) output << a.damage.cyclesEvaluated; 
        output << ',';
        writeNumber(output, a.temperatureKelvin);
        output << ',' << csv(a.diagnostic) << '\n';
    }
    if (!output) return failure(FatigueReportWriteStatus::StreamError, "Failed while writing CSV summary.");
    return {FatigueReportWriteStatus::Success, "CSV summary written."};
}

FatigueReportWriteResult FatigueReportWriter::writeCsvCycles(
    const FatigueReport& report, std::ostream& output) {
    if (!validReport(report)) return failure(FatigueReportWriteStatus::InvalidReport, "Report has no serializable selection.");
    output << "location,entity_id,integration_point,section_point,cycle_index,start_time,end_time,start_value,end_value,range,mean,count,start_sample_index,end_sample_index\n";
    for (const auto& row : report.positions) {
        if (!row.hasDamage) continue;
        const auto& cycles = row.analysis.cycleCounting.cycles;
        for (std::size_t i=0; i<cycles.size(); ++i) {
            const auto& c=cycles[i];
            output << locationName(row.position.location) << ',' << row.position.entityId << ','
                   << row.position.integrationPoint << ',' << row.position.sectionPoint << ',' << i << ',';
            writeNumber(output,c.start.time); output << ',';
            writeNumber(output,c.end.time); output << ',';
            writeNumber(output,c.start.value); output << ',';
            writeNumber(output,c.end.value); output << ',';
            writeNumber(output,c.range); output << ',';
            writeNumber(output,c.mean); output << ',';
            writeNumber(output,c.count); output << ','
                   << c.start.historySampleIndex << ',' << c.end.historySampleIndex << '\n';
        }
    }
    if (!output) return failure(FatigueReportWriteStatus::StreamError, "Failed while writing CSV cycle details.");
    return {FatigueReportWriteStatus::Success, "CSV cycle details written."};
}

FatigueReportWriteResult FatigueReportWriter::writeJson(
    const FatigueReport& report, std::ostream& output, bool includeCycles) {
    if (!validReport(report)) return failure(FatigueReportWriteStatus::InvalidReport, "Report has no serializable selection.");
    output << "{\"schema\":\"openmodel.fatigue-report.v1\",\"status\":" << json(reportStatus(report.status))
           << ",\"field\":" << json(report.fieldName)
           << ",\"source\":{\"solver\":" << json(report.source.solverName)
           << ",\"version\":" << json(report.source.solverVersion)
           << ",\"format\":" << json(report.source.resultFormat)
           << ",\"name\":" << json(report.source.sourceName)
           << ",\"model_fingerprint\":" << json(report.source.modelFingerprint) << '}'
           << ",\"summary\":{\"successful_positions\":" << report.successfulPositions
           << ",\"failed_positions\":" << report.failedPositions
           << ",\"zero_damage_positions\":" << report.zeroDamagePositions
           << ",\"positive_damage_positions\":" << report.positiveDamagePositions
           << ",\"has_maximum_damage\":" << (report.hasMaximumDamage ? "true" : "false")
           << ",\"maximum_damage_index\":";
    if (report.hasMaximumDamage) output << report.maximumDamageIndex; else output << "null";
    output << ",\"maximum_damage\":";
    if (report.hasMaximumDamage) writeNumber(output,report.maximumDamage); else output << "null";
    output << "},\"positions\":[";
    for (std::size_t i=0;i<report.positions.size();++i) {
        if (i) output << ',';
        const auto& row=report.positions[i];
        const auto& a=row.analysis;
        output << "{\"label\":" << json(row.label) << ",\"position\":";
        writePosition(output,row.position);
        output << ",\"analysis_status\":" << json(analysisStatus(a.status))
               << ",\"has_damage\":" << (row.hasDamage ? "true" : "false")
               << ",\"damage\":";
        if (row.hasDamage) writeNumber(output,row.damage); else output << "null";
        output << ",\"block_life\":";
        if (row.hasDamage) writeNumber(output,a.damage.blockLife); else output << "null";
        output << ",\"cycles_evaluated\":";
        if (row.hasDamage) output << a.damage.cyclesEvaluated; else output << "null";
        output << ",\"temperature_kelvin\":"; writeNumber(output,a.temperatureKelvin);
        output << ",\"diagnostic\":" << json(a.diagnostic);
        if (includeCycles && row.hasDamage) {
            output << ",\"cycles\":[";
            for (std::size_t k=0;k<a.cycleCounting.cycles.size();++k) {
                if(k) output << ',';
                const auto& c=a.cycleCounting.cycles[k];
                output << "{\"start_time\":"; writeNumber(output,c.start.time);
                output << ",\"end_time\":"; writeNumber(output,c.end.time);
                output << ",\"start_value\":"; writeNumber(output,c.start.value);
                output << ",\"end_value\":"; writeNumber(output,c.end.value);
                output << ",\"range\":"; writeNumber(output,c.range);
                output << ",\"mean\":"; writeNumber(output,c.mean);
                output << ",\"count\":"; writeNumber(output,c.count);
                output << ",\"start_sample_index\":" << c.start.historySampleIndex
                       << ",\"end_sample_index\":" << c.end.historySampleIndex << '}';
            }
            output << ']';
        }
        output << '}';
    }
    output << "],\"diagnostic\":" << json(report.diagnostic) << "}\n";
    if (!output) return failure(FatigueReportWriteStatus::StreamError, "Failed while writing JSON report.");
    return {FatigueReportWriteStatus::Success, "JSON report written."};
}

} // namespace Fatigue
} // namespace OpenModel
