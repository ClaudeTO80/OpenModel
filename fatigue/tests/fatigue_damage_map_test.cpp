#include <OpenModel/Fatigue/FatigueDamageMap.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    MeshData mesh;
    const auto n0 = mesh.addNode(101, 0.0, 0.0, 0.0);
    const auto n1 = mesh.addNode(102, 2.0, 0.0, 0.0);
    const auto n2 = mesh.addNode(103, 0.0, 2.0, 0.0);
    mesh.addElement("surface", ElementType::Triangle3, 501, {n0, n1, n2});

    FatigueSpectrumReport report;
    report.status = FatigueSpectrumReportStatus::PartialFailure;

    FatigueSpectrumPositionResult node;
    node.position.location = Fatigue::ResultLocation::Node;
    node.position.entityId = 101;
    node.label = "node 101";
    node.hasDamage = true;
    node.damage = 0.125;
    node.spectrum.status = FatigueSpectrumStatus::Success;
    report.positions.push_back(node);

    FatigueSpectrumPositionResult element;
    element.position.location = Fatigue::ResultLocation::Element;
    element.position.entityId = 501;
    element.label = "element 501";
    element.hasDamage = true;
    element.damage = 0.25;
    element.spectrum.status = FatigueSpectrumStatus::Success;
    report.positions.push_back(element);

    FatigueSpectrumPositionResult ip;
    ip.position.location = Fatigue::ResultLocation::IntegrationPoint;
    ip.position.entityId = 501;
    ip.position.integrationPoint = 1;
    ip.label = "element 501 IP1";
    ip.hasDamage = false;
    ip.spectrum.status = FatigueSpectrumStatus::BlockAnalysisFailed;
    report.positions.push_back(ip);

    const auto map = FatigueDamageMapBuilder::build(report, mesh);
    assert(map.status == FatigueDamageMapStatus::PartialMeshMatch);
    assert(map.entries.size() == 3);
    assert(map.matchedCoordinates == 2);
    assert(map.unavailableCoordinates == 1);
    assert(map.entriesWithDamage == 2);
    assert(map.entriesWithoutDamage == 1);

    assert(map.entries[0].coordinateKind == DamageMapCoordinateKind::Node);
    assert(map.entries[0].coordinates[0] == 0.0);
    assert(map.entries[1].coordinateKind == DamageMapCoordinateKind::ElementCentroid);
    assert(std::abs(map.entries[1].coordinates[0] - 2.0 / 3.0) < 1e-12);
    assert(std::abs(map.entries[1].coordinates[1] - 2.0 / 3.0) < 1e-12);
    assert(!map.entries[2].hasCoordinates);
    assert(map.entries[2].coordinateKind == DamageMapCoordinateKind::Unavailable);
    assert(!map.entries[2].hasDamage);
    assert(map.entries[2].damage == 0.0);

    FatigueSpectrumReport empty;
    assert(FatigueDamageMapBuilder::build(empty, mesh).status ==
           FatigueDamageMapStatus::EmptyReport);
    return 0;
}
