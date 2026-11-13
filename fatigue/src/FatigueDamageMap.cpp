#include <OpenModel/Fatigue/FatigueDamageMap.hpp>

#include <algorithm>

namespace OpenModel { namespace Fatigue {
namespace {
bool findElement(const MeshData& mesh, ElementId id, std::array<double,3>& center) {
    for (const auto& block : mesh.getElementBlocks()) {
        const auto it = std::find(block.ids.begin(), block.ids.end(), id);
        if (it == block.ids.end() || block.nodesPerElement == 0) continue;
        const std::size_t element = static_cast<std::size_t>(it - block.ids.begin());
        const std::size_t begin = element * block.nodesPerElement;
        if (begin + block.nodesPerElement > block.connectivity.size()) return false;
        center = {{0.0, 0.0, 0.0}};
        for (std::size_t n = 0; n < block.nodesPerElement; ++n) {
            const auto xyz = mesh.nodeCoordinates(block.connectivity[begin + n]);
            for (std::size_t d = 0; d < 3; ++d) center[d] += xyz[d];
        }
        for (double& v : center) v /= static_cast<double>(block.nodesPerElement);
        return true;
    }
    return false;
}
}

FatigueDamageMap FatigueDamageMapBuilder::build(
    const FatigueSpectrumReport& report, const MeshData& mesh) {
    FatigueDamageMap map;
    if (report.positions.empty()) {
        map.status = FatigueDamageMapStatus::EmptyReport;
        map.diagnostic = "The spectrum report contains no positions.";
        return map;
    }
    map.entries.reserve(report.positions.size());
    for (const auto& row : report.positions) {
        FatigueDamageMapEntry entry;
        entry.position = row.position;
        entry.label = row.label;
        entry.hasDamage = row.hasDamage;
        entry.damage = row.damage;
        entry.analysisStatus = row.spectrum.status;
        if (row.hasDamage) ++map.entriesWithDamage;
        else ++map.entriesWithoutDamage;

        if (row.position.location == ResultLocation::Node) {
            const auto id = static_cast<NodeId>(row.position.entityId);
            if (mesh.hasNodeId(id)) {
                entry.coordinates = mesh.nodeCoordinates(mesh.nodeIndex(id));
                entry.coordinateKind = DamageMapCoordinateKind::Node;
                entry.hasCoordinates = true;
            }
        } else if (row.position.location == ResultLocation::Element) {
            entry.hasCoordinates = findElement(mesh, static_cast<ElementId>(row.position.entityId), entry.coordinates);
            if (entry.hasCoordinates) entry.coordinateKind = DamageMapCoordinateKind::ElementCentroid;
        }
        // Integration-point coordinates are deliberately not guessed from the element centroid.
        if (entry.hasCoordinates) ++map.matchedCoordinates;
        else ++map.unavailableCoordinates;
        map.entries.push_back(std::move(entry));
    }
    if (map.matchedCoordinates == map.entries.size()) {
        map.status = FatigueDamageMapStatus::Success;
        map.diagnostic = "Coordinates resolved for every report position.";
    } else if (map.matchedCoordinates == 0) {
        map.status = FatigueDamageMapStatus::NoMeshMatch;
        map.diagnostic = "No exact nodal or element coordinate mapping was available.";
    } else {
        map.status = FatigueDamageMapStatus::PartialMeshMatch;
        map.diagnostic = "Some positions have no exact coordinate representation in the mesh.";
    }
    return map;
}
} } // namespace OpenModel::Fatigue
