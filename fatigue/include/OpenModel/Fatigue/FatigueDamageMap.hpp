#pragma once

#include <OpenModel/Fatigue/FatigueSpectrumReport.hpp>
#include <OpenModel/MeshData.hpp>

#include <array>
#include <string>
#include <vector>

namespace OpenModel { namespace Fatigue {

enum class DamageMapCoordinateKind { Unavailable, Node, ElementCentroid };

struct FatigueDamageMapEntry {
    ResultPosition position;
    std::string label;
    bool hasDamage = false;
    double damage = 0.0;
    FatigueSpectrumStatus analysisStatus = FatigueSpectrumStatus::EmptySpectrum;
    DamageMapCoordinateKind coordinateKind = DamageMapCoordinateKind::Unavailable;
    bool hasCoordinates = false;
    std::array<double, 3> coordinates{{0.0, 0.0, 0.0}};
};

enum class FatigueDamageMapStatus {
    Success, EmptyReport, NoMeshMatch, PartialMeshMatch
};

/**
 * Spatial projection of a spectrum report onto mesh entities.
 * Element coordinates are explicitly centroids; integration-point coordinates
 * are unavailable unless a future importer supplies their actual coordinates.
 */
struct FatigueDamageMap {
    FatigueDamageMapStatus status = FatigueDamageMapStatus::EmptyReport;
    std::vector<FatigueDamageMapEntry> entries;
    std::size_t matchedCoordinates = 0;
    std::size_t unavailableCoordinates = 0;
    std::size_t entriesWithDamage = 0;
    std::size_t entriesWithoutDamage = 0;
    std::string diagnostic;
};

class FatigueDamageMapBuilder final {
public:
    static FatigueDamageMap build(
        const FatigueSpectrumReport& report,
        const MeshData& mesh);
};

} } // namespace OpenModel::Fatigue
