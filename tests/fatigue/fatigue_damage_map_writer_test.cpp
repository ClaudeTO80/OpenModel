#include <OpenModel/Fatigue/FatigueReportWriter.hpp>

#include <cassert>
#include <sstream>
#include <string>

using namespace OpenModel::Fatigue;

int main() {
    FatigueDamageMap map;
    map.status = FatigueDamageMapStatus::PartialMeshMatch;
    map.matchedCoordinates = 1;
    map.unavailableCoordinates = 1;
    map.entriesWithDamage = 1;
    map.entriesWithoutDamage = 1;

    FatigueDamageMapEntry node;
    node.position.location = ResultLocation::Node;
    node.position.entityId = 12;
    node.label = "node, \"A\"";
    node.hasDamage = true;
    node.damage = 0.125;
    node.analysisStatus = FatigueSpectrumStatus::Success;
    node.coordinateKind = DamageMapCoordinateKind::Node;
    node.hasCoordinates = true;
    node.coordinates = {{1.0, 2.0, 3.0}};
    map.entries.push_back(node);

    FatigueDamageMapEntry ip;
    ip.position.location = ResultLocation::IntegrationPoint;
    ip.position.entityId = 99;
    ip.position.integrationPoint = 2;
    ip.label = "IP 2";
    ip.analysisStatus = FatigueSpectrumStatus::BlockAnalysisFailed;
    map.entries.push_back(ip);
    map.diagnostic = "partial mapping";

    std::ostringstream csv;
    assert(FatigueReportWriter::writeDamageMapCsv(map, csv));
    assert(csv.str().find("\"node, \"\"A\"\"\",true,0.125") != std::string::npos);
    assert(csv.str().find("integration_point,99,2,0") != std::string::npos);

    std::ostringstream json;
    assert(FatigueReportWriter::writeDamageMapJson(map, json));
    assert(json.str().find("\"schema\":\"openmodel.fatigue-damage-map.v1\"") != std::string::npos);
    assert(json.str().find("\"analysis_status\":\"block_analysis_failed\"") != std::string::npos);
    assert(json.str().find("\"coordinates\":null") != std::string::npos);
    assert(json.str().find("\"damage\":null") != std::string::npos);

    FatigueDamageMap empty;
    assert(!FatigueReportWriter::writeDamageMapJson(empty, json));
    return 0;
}
