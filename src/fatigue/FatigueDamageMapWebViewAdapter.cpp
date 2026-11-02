#include <OpenModel/Fatigue/FatigueDamageMapWebViewAdapter.hpp>

#include <cmath>
#include <unordered_map>

namespace OpenModel { namespace Fatigue {

DamageMapViewerResult FatigueDamageMapWebViewAdapter::makeElementField(
    const FatigueDamageMap& map, const MeshData& mesh, const std::string& fieldName) {
    DamageMapViewerResult result;
    if (map.entries.empty()) {
        result.status = DamageMapViewerStatus::EmptyMap;
        result.diagnostic = "Damage map contains no entries.";
        return result;
    }
    if (fieldName.empty()) {
        result.status = DamageMapViewerStatus::InvalidDamage;
        result.diagnostic = "Viewer field name must not be empty.";
        return result;
    }

    std::unordered_map<ElementId, double> values;
    values.reserve(map.entries.size());
    for (const auto& entry : map.entries) {
        if (entry.position.location != ResultLocation::Element) {
            result.status = DamageMapViewerStatus::UnsupportedLocation;
            result.diagnostic = "The element viewer field accepts element positions only; nodal and integration-point data are not interpolated.";
            return result;
        }
        if (!entry.hasDamage) {
            result.status = DamageMapViewerStatus::MissingDamage;
            result.diagnostic = "At least one selected element has no valid damage result.";
            return result;
        }
        if (!std::isfinite(entry.damage) || entry.damage < 0.0) {
            result.status = DamageMapViewerStatus::InvalidDamage;
            result.diagnostic = "Damage values must be finite and non-negative.";
            return result;
        }
        if (!values.emplace(static_cast<ElementId>(entry.position.entityId), entry.damage).second) {
            result.status = DamageMapViewerStatus::DuplicateElement;
            result.diagnostic = "The damage map contains duplicate element IDs.";
            return result;
        }
    }

    result.field.name = fieldName;
    result.field.values.reserve(mesh.getNumElements());
    for (const auto& block : mesh.getElementBlocks()) {
        for (const ElementId id : block.ids) {
            const auto it = values.find(id);
            if (it == values.end()) {
                result.status = DamageMapViewerStatus::MissingElement;
                result.field.values.clear();
                result.diagnostic = "The damage map does not cover every mesh element.";
                return result;
            }
            result.field.values.push_back(it->second);
        }
    }
    if (result.field.values.size() != mesh.getNumElements() || values.size() != mesh.getNumElements()) {
        result.status = DamageMapViewerStatus::MissingElement;
        result.field.values.clear();
        result.diagnostic = "Damage map and mesh element counts do not match.";
        return result;
    }
    result.status = DamageMapViewerStatus::Success;
    result.diagnostic = "Complete element damage field ready for flat-shaded WebGL rendering.";
    return result;
}

} } // namespace OpenModel::Fatigue
