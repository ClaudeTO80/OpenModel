#include <OpenModel/Fatigue/FatigueReportWriter.hpp>

#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace OpenModel { namespace Fatigue { namespace {
const char* mapStatus(FatigueDamageMapStatus s) {
    switch (s) {
    case FatigueDamageMapStatus::Success: return "success";
    case FatigueDamageMapStatus::EmptyReport: return "empty_report";
    case FatigueDamageMapStatus::NoMeshMatch: return "no_mesh_match";
    case FatigueDamageMapStatus::PartialMeshMatch: return "partial_mesh_match";
    }
    return "unknown";
}
const char* coordinateKind(DamageMapCoordinateKind k) {
    switch (k) {
    case DamageMapCoordinateKind::Unavailable: return "unavailable";
    case DamageMapCoordinateKind::Node: return "node";
    case DamageMapCoordinateKind::ElementCentroid: return "element_centroid";
    }
    return "unknown";
}
const char* analysisStatus(FatigueSpectrumStatus s) {
    switch (s) {
    case FatigueSpectrumStatus::Success: return "success";
    case FatigueSpectrumStatus::EmptySpectrum: return "empty_spectrum";
    case FatigueSpectrumStatus::InvalidBlock: return "invalid_block";
    case FatigueSpectrumStatus::BlockAnalysisFailed: return "block_analysis_failed";
    case FatigueSpectrumStatus::NumericalOverflow: return "numerical_overflow";
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
std::string csvQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { out += c; if (c == '"') out += '"'; }
    return out + '"';
}
std::string jsonQuote(const std::string& s) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out << "\\\""; break;
        case '\\': out << "\\\\"; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (c < 0x20) out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << int(c) << std::dec;
            else out << char(c);
        }
    }
    return out.str() + '"';
}
void writeNumber(std::ostream& out, double v) {
    if (std::isfinite(v)) out << std::setprecision(17) << v;
    else out << "null";
}
void writePosition(std::ostream& out, const ResultPosition& p) {
    out << "{\"location\":" << jsonQuote(locationName(p.location))
        << ",\"entity_id\":" << p.entityId
        << ",\"integration_point\":" << p.integrationPoint
        << ",\"section_point\":" << p.sectionPoint << '}';
}
} // namespace

FatigueReportWriteResult FatigueReportWriter::writeDamageMapCsv(
    const FatigueDamageMap& map, std::ostream& out) {
    if (map.status == FatigueDamageMapStatus::EmptyReport || map.entries.empty())
        return {FatigueReportWriteStatus::InvalidReport, "Damage map has no entries."};
    out << "map_status,location,entity_id,integration_point,section_point,label,has_damage,damage,analysis_status,coordinate_kind,has_coordinates,x,y,z\n";
    for (const auto& e : map.entries) {
        out << mapStatus(map.status) << ',' << locationName(e.position.location) << ','
            << e.position.entityId << ',' << e.position.integrationPoint << ','
            << e.position.sectionPoint << ',' << csvQuote(e.label) << ','
            << (e.hasDamage ? "true" : "false") << ',';
        if (e.hasDamage) writeNumber(out, e.damage);
        out << ',' << analysisStatus(e.analysisStatus) << ','
            << coordinateKind(e.coordinateKind) << ',' << (e.hasCoordinates ? "true" : "false") << ',';
        if (e.hasCoordinates) {
            writeNumber(out, e.coordinates[0]); out << ',';
            writeNumber(out, e.coordinates[1]); out << ',';
            writeNumber(out, e.coordinates[2]);
        } else {
            out << ",,";
        }
        out << '\n';
    }
    if (!out) return {FatigueReportWriteStatus::StreamError, "Failed writing damage map CSV."};
    return {FatigueReportWriteStatus::Success, "Damage map CSV written."};
}

FatigueReportWriteResult FatigueReportWriter::writeDamageMapJson(
    const FatigueDamageMap& map, std::ostream& out) {
    if (map.status == FatigueDamageMapStatus::EmptyReport || map.entries.empty())
        return {FatigueReportWriteStatus::InvalidReport, "Damage map has no entries."};
    out << "{\"schema\":\"openmodel.fatigue-damage-map.v1\",\"status\":"
        << jsonQuote(mapStatus(map.status))
        << ",\"summary\":{\"matched_coordinates\":" << map.matchedCoordinates
        << ",\"unavailable_coordinates\":" << map.unavailableCoordinates
        << ",\"entries_with_damage\":" << map.entriesWithDamage
        << ",\"entries_without_damage\":" << map.entriesWithoutDamage
        << "},\"entries\":[";
    for (std::size_t i = 0; i < map.entries.size(); ++i) {
        if (i) out << ',';
        const auto& e = map.entries[i];
        out << "{\"position\":";
        writePosition(out, e.position);
        out << ",\"label\":" << jsonQuote(e.label)
            << ",\"has_damage\":" << (e.hasDamage ? "true" : "false")
            << ",\"damage\":";
        if (e.hasDamage) writeNumber(out, e.damage); else out << "null";
        out << ",\"analysis_status\":" << static_cast<int>(e.analysisStatus)
            << ",\"coordinate_kind\":" << jsonQuote(coordinateKind(e.coordinateKind))
            << ",\"coordinates\":";
        if (e.hasCoordinates) {
            out << '['; writeNumber(out, e.coordinates[0]); out << ',';
            writeNumber(out, e.coordinates[1]); out << ',';
            writeNumber(out, e.coordinates[2]); out << ']';
        } else out << "null";
        out << '}';
    }
    out << "],\"diagnostic\":" << jsonQuote(map.diagnostic) << "}\n";
    if (!out) return {FatigueReportWriteStatus::StreamError, "Failed writing damage map JSON."};
    return {FatigueReportWriteStatus::Success, "Damage map JSON written."};
}

} } // namespace OpenModel::Fatigue
