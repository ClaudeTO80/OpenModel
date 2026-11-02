#include <OpenModel/Fatigue/FatigueReportWriter.hpp>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace OpenModel { namespace Fatigue { namespace {
const char* rs(FatigueSpectrumReportStatus s) {
    switch(s) {
    case FatigueSpectrumReportStatus::Success: return "success";
    case FatigueSpectrumReportStatus::EmptySelection: return "empty_selection";
    case FatigueSpectrumReportStatus::InvalidInput: return "invalid_input";
    case FatigueSpectrumReportStatus::PartialFailure: return "partial_failure";
    case FatigueSpectrumReportStatus::AllPositionsFailed: return "all_positions_failed";
    } return "unknown";
}
const char* ss(FatigueSpectrumStatus s) {
    switch(s) {
    case FatigueSpectrumStatus::Success: return "success";
    case FatigueSpectrumStatus::EmptySpectrum: return "empty_spectrum";
    case FatigueSpectrumStatus::InvalidBlock: return "invalid_block";
    case FatigueSpectrumStatus::BlockAnalysisFailed: return "block_analysis_failed";
    case FatigueSpectrumStatus::NumericalOverflow: return "numerical_overflow";
    } return "unknown";
}
const char* as(FatigueAnalysisStatus s) {
    switch(s) {
    case FatigueAnalysisStatus::Success: return "success";
    case FatigueAnalysisStatus::InvalidMaterial: return "invalid_material";
    case FatigueAnalysisStatus::MissingSNcurve: return "missing_sn_curve";
    case FatigueAnalysisStatus::HistoryBuildFailed: return "history_build_failed";
    case FatigueAnalysisStatus::NonStressHistory: return "non_stress_history";
    case FatigueAnalysisStatus::CycleCountingFailed: return "cycle_counting_failed";
    case FatigueAnalysisStatus::DamageEvaluationFailed: return "damage_evaluation_failed";
    } return "unknown";
}
const char* loc(ResultLocation l) {
    switch(l) {
    case ResultLocation::Node: return "node";
    case ResultLocation::Element: return "element";
    case ResultLocation::IntegrationPoint: return "integration_point";
    } return "unknown";
}
std::string cq(const std::string& s) {
    std::string r = "\"";
    for(char c:s) { r += c; if(c=='"') r += '"'; }
    return r + '"';
}
std::string jq(const std::string& s) {
    std::ostringstream o; o << '"';
    for(unsigned char c:s) {
        switch(c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\b': o << "\\b"; break;
        case '\f': o << "\\f"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default:
            if(c<0x20) o << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c) << std::dec;
            else o << char(c);
        }
    }
    return o.str() + '"';
}
void num(std::ostream& o,double v) { if(std::isfinite(v)) o<<std::setprecision(17)<<v; else o<<"null"; }
void pos(std::ostream& o,const ResultPosition& p) {
    o << "{\"location\":" << jq(loc(p.location)) << ",\"entity_id\":" << p.entityId
      << ",\"integration_point\":" << p.integrationPoint << ",\"section_point\":" << p.sectionPoint << '}';
}
FatigueReportWriteResult invalid() { return {FatigueReportWriteStatus::InvalidReport,"Spectrum report has no serializable selection."}; }
bool valid(const FatigueSpectrumReport& r) {
    return r.status!=FatigueSpectrumReportStatus::EmptySelection && r.status!=FatigueSpectrumReportStatus::InvalidInput;
}
} // anonymous

FatigueReportWriteResult FatigueReportWriter::writeCsvSpectrumSummary(const FatigueSpectrumReport& r,std::ostream& o) {
    if(!valid(r)) return invalid();
    o<<"report_status,location,entity_id,integration_point,section_point,label,spectrum_status,has_damage,damage,spectrum_life,block_count,diagnostic\n";
    for(const auto& p:r.positions) {
        o<<rs(r.status)<<','<<loc(p.position.location)<<','<<p.position.entityId<<','<<p.position.integrationPoint<<','
         <<p.position.sectionPoint<<','<<cq(p.label)<<','<<ss(p.spectrum.status)<<','<<(p.hasDamage?"true":"false")<<',';
        if(p.hasDamage) num(o,p.damage); o<<',';
        if(p.hasDamage) num(o,p.spectrum.spectrumLife);
        o<<','<<p.spectrum.blocks.size()<<','<<cq(p.spectrum.diagnostic)<<'\n';
    }
    if(!o) return {FatigueReportWriteStatus::StreamError,"Failed writing spectrum summary CSV."};
    return {FatigueReportWriteStatus::Success,"Spectrum summary CSV written."};
}
FatigueReportWriteResult FatigueReportWriter::writeCsvSpectrumBlocks(const FatigueSpectrumReport& r,std::ostream& o) {
    if(!valid(r)) return invalid();
    o<<"location,entity_id,integration_point,section_point,position_label,block_index,block_label,repetitions,analysis_status,has_damage,damage_per_block,damage_contribution,cycles_evaluated,diagnostic\n";
    for(const auto& p:r.positions) for(std::size_t i=0;i<p.spectrum.blocks.size();++i) {
        const auto& b=p.spectrum.blocks[i]; const bool ok=p.hasDamage && bool(b.analysis);
        o<<loc(p.position.location)<<','<<p.position.entityId<<','<<p.position.integrationPoint<<','<<p.position.sectionPoint<<','
         <<cq(p.label)<<','<<i<<','<<cq(b.label)<<','; num(o,b.repetitions);
        o<<','<<as(b.analysis.status)<<','<<(ok?"true":"false")<<',';
        if(ok) num(o,b.damagePerBlock); o<<',';
        if(ok) num(o,b.damageContribution); o<<',';
        if(ok) o<<b.analysis.damage.cyclesEvaluated;
        o<<','<<cq(b.analysis.diagnostic)<<'\n';
    }
    if(!o) return {FatigueReportWriteStatus::StreamError,"Failed writing spectrum block CSV."};
    return {FatigueReportWriteStatus::Success,"Spectrum block CSV written."};
}
FatigueReportWriteResult FatigueReportWriter::writeSpectrumJson(const FatigueSpectrumReport& r,std::ostream& o,bool cycles) {
    if(!valid(r)) return invalid();
    o<<"{\"schema\":\"openmodel.fatigue-spectrum-report.v1\",\"status\":"<<jq(rs(r.status))
     <<",\"summary\":{\"successful_positions\":"<<r.successfulPositions<<",\"failed_positions\":"<<r.failedPositions
     <<",\"has_maximum_damage\":"<<(r.hasMaximumDamage?"true":"false")<<",\"maximum_damage_index\":";
    if(r.hasMaximumDamage) o<<r.maximumDamageIndex; else o<<"null";
    o<<",\"maximum_damage\":"; if(r.hasMaximumDamage) num(o,r.maximumDamage); else o<<"null";
    o<<"},\"positions\":[";
    for(std::size_t i=0;i<r.positions.size();++i) {
        if(i) o<<','; const auto& p=r.positions[i];
        o<<"{\"label\":"<<jq(p.label)<<",\"position\":"; pos(o,p.position);
        o<<",\"spectrum_status\":"<<jq(ss(p.spectrum.status))<<",\"has_damage\":"<<(p.hasDamage?"true":"false")<<",\"damage\":";
        if(p.hasDamage) num(o,p.damage); else o<<"null";
        o<<",\"spectrum_life\":"; if(p.hasDamage) num(o,p.spectrum.spectrumLife); else o<<"null";
        o<<",\"blocks\":[";
        for(std::size_t j=0;j<p.spectrum.blocks.size();++j) {
            if(j) o<<','; const auto& b=p.spectrum.blocks[j]; const auto& a=b.analysis;
            const bool ok=p.hasDamage && bool(a);
            o<<"{\"label\":"<<jq(b.label)<<",\"repetitions\":"; num(o,b.repetitions);
            o<<",\"analysis_status\":"<<jq(as(a.status))<<",\"damage_per_block\":";
            if(ok) num(o,b.damagePerBlock); else o<<"null";
            o<<",\"damage_contribution\":"; if(ok) num(o,b.damageContribution); else o<<"null";
            o<<",\"cycles_evaluated\":"; if(ok) o<<a.damage.cyclesEvaluated; else o<<"null";
            o<<",\"diagnostic\":"<<jq(a.diagnostic);
            if(cycles && ok) {
                o<<",\"cycles\":[";
                for(std::size_t k=0;k<a.cycleCounting.cycles.size();++k) {
                    if(k) o<<','; const auto& c=a.cycleCounting.cycles[k];
                    o<<"{\"start_time\":"; num(o,c.start.time); o<<",\"end_time\":"; num(o,c.end.time);
                    o<<",\"range\":"; num(o,c.range); o<<",\"mean\":"; num(o,c.mean); o<<",\"count\":"; num(o,c.count);
                    o<<",\"start_sample_index\":"<<c.start.historySampleIndex<<",\"end_sample_index\":"<<c.end.historySampleIndex<<'}';
                }
                o<<']';
            }
            o<<'}';
        }
        o<<"],\"diagnostic\":"<<jq(p.spectrum.diagnostic)<<'}';
    }
    o<<"],\"diagnostic\":"<<jq(r.diagnostic)<<"}\n";
    if(!o) return {FatigueReportWriteStatus::StreamError,"Failed writing spectrum JSON."};
    return {FatigueReportWriteStatus::Success,"Spectrum JSON written."};
}
} } // namespace OpenModel::Fatigue
