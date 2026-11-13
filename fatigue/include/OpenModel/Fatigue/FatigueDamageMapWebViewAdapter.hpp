#pragma once

#include <OpenModel/Fatigue/FatigueDamageMap.hpp>
#include <OpenModel/WebViewWriter.hpp>

#include <string>

namespace OpenModel { namespace Fatigue {

enum class DamageMapViewerStatus {
    Success, EmptyMap, UnsupportedLocation, MissingDamage,
    DuplicateElement, MissingElement, InvalidDamage
};

struct DamageMapViewerResult {
    DamageMapViewerStatus status = DamageMapViewerStatus::EmptyMap;
    WebElementField field;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == DamageMapViewerStatus::Success; }
};

/**
 * Converts an element-only damage map to the viewer's native flat-shaded
 * element scalar field. Requires exactly one valid damage value for every
 * mesh element; it never interpolates nodal/IP values or fills missing values.
 */
class FatigueDamageMapWebViewAdapter final {
public:
    static DamageMapViewerResult makeElementField(
        const FatigueDamageMap& map,
        const MeshData& mesh,
        const std::string& fieldName = "Fatigue damage");
};

} } // namespace OpenModel::Fatigue
