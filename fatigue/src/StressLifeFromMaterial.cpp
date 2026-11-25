/**
 * @file StressLifeFromMaterial.cpp
 * @brief Implementation of the material-to-fatigue bridge: Basquin S-N curves and position lists.
 */

#include <OpenModel/Fatigue/StressLifeFromMaterial.hpp>

#include <cmath>
#include <cstddef>
#include <sstream>
#include <utility>

namespace OpenModel {
namespace Fatigue {

namespace {

/** Reversals to failure 2N covered by the table: from 10^kMinimumLog10Reversals to 10^kMaximumLog10Reversals. */
constexpr double kMinimumLog10Reversals = 0.0;
constexpr double kMaximumLog10Reversals = 15.0;
/** Spacing of the table in decades of reversals: half a decade keeps the table small and the interpolation exact. */
constexpr double kStepLog10 = 0.5;

bool positiveFinite(double value) {
    return std::isfinite(value) && value > 0.0;
}

} // namespace

PhysicalUnit pressureUnit(const std::string& symbol, double scaleToSI) {
    PhysicalUnit unit;
    unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};   // known, length, mass, time, current, temperature, amount, luminous
    unit.symbol = symbol;
    unit.scaleToSI = scaleToSI;
    unit.offsetToSI = 0.0;
    return unit;
}

bool stressLifeCurveFromBasquin(
    const MaterialRecord& record,
    double temperatureKelvin,
    const PhysicalUnit& amplitudeUnit,
    FatigueMaterial& out,
    std::string& error) {
    const std::string id = record.elastic.id;
    const MaterialFatigueData& fatigue = record.fatigue;
    if (!fatigue.fatigueStrengthCoefficientPa || !fatigue.fatigueStrengthExponent) {
        error = "material '" + id + "' has no Basquin constants (fatigue strength coefficient sigma_f' and exponent b)";
        return false;
    }
    const double sigmaF = *fatigue.fatigueStrengthCoefficientPa;
    const double b = *fatigue.fatigueStrengthExponent;
    if (!positiveFinite(sigmaF)) {
        error = "material '" + id + "': sigma_f' must be a positive finite stress";
        return false;
    }
    if (!std::isfinite(b) || !(b < 0.0)) {
        error = "material '" + id + "': the Basquin exponent b must be negative";
        return false;
    }
    if (!positiveFinite(amplitudeUnit.scaleToSI)) {
        error = "the stress unit '" + amplitudeUnit.symbol + "' has no positive scale to SI";
        return false;
    }
    if (!positiveFinite(temperatureKelvin)) {
        error = "the temperature must be a positive number of kelvin";
        return false;
    }

    // Points go from the smallest amplitude (longest life) to the largest, as FatigueCurve requires: the reversals
    // decrease along the loop, and with b < 0 the amplitude then increases while the life decreases.
    const auto steps = static_cast<std::size_t>(std::lround((kMaximumLog10Reversals - kMinimumLog10Reversals) / kStepLog10));
    std::vector<FatigueCurvePoint> points;
    points.reserve(steps + 1);
    for (std::size_t k = 0; k <= steps; ++k) {
        const double log10Reversals = kMaximumLog10Reversals - static_cast<double>(k) * kStepLog10;
        // Basquin: sigma_a = sigma_f' (2N)^b, evaluated in Pa and then expressed in the unit of the results.
        const double amplitudePa = sigmaF * std::pow(10.0, b * log10Reversals);
        const double cyclesToFailure = 0.5 * std::pow(10.0, log10Reversals);   // N = (2N) / 2
        points.push_back({amplitudePa / amplitudeUnit.scaleToSI, cyclesToFailure});
    }

    std::vector<FatigueCurveSlice> slices{{temperatureKelvin, points}};
    FatigueCurve curve;
    std::string diagnostic;
    if (!FatigueCurve::create(FatigueCurveKind::StressLifeSN, amplitudeUnit, std::move(slices), curve, diagnostic)) {
        error = "material '" + id + "': the Basquin curve is not valid: " + diagnostic;
        return false;
    }

    FatigueMaterial material;
    material.name = record.elastic.name;
    material.materialId = id;
    material.source = "OpenModel material database: Basquin law sigma_a = sigma_f' (2N)^b";
    material.curves.push_back(std::move(curve));
    if (material.validate(diagnostic) != FatigueCurveStatus::Success) {
        error = "material '" + id + "': " + diagnostic;
        return false;
    }
    out = std::move(material);
    return true;
}

std::string positionLabel(const ResultPosition& position) {
    std::ostringstream text;
    switch (position.location) {
    case ResultLocation::Node:
        text << "node:" << position.entityId;
        break;
    case ResultLocation::Element:
        text << "element:" << position.entityId;
        break;
    case ResultLocation::IntegrationPoint:
        text << "ip:" << position.entityId << ":" << position.integrationPoint;
        break;
    }
    return text.str();
}

bool parsePositionList(const std::string& text, std::vector<ResultPosition>& positions, std::string& error) {
    positions.clear();
    std::string item;
    // Reads one item: trims it, ignores it when blank, otherwise parses it. Returns false on the first bad item.
    auto readItem = [&]() -> bool {
        const auto begin = item.find_first_not_of(" \t\r");
        if (begin == std::string::npos) {
            item.clear();
            return true;
        }
        const auto end = item.find_last_not_of(" \t\r");
        const std::string token = item.substr(begin, end - begin + 1);
        item.clear();
        ResultPosition position;
        const SpecificationResult parsed = parsePosition(token, position);
        if (!parsed) {
            error = "position '" + token + "': " + parsed.error;
            return false;
        }
        positions.push_back(position);
        return true;
    };
    for (const char c : text) {
        if (c == ';' || c == '\n') {
            if (!readItem()) return false;
        } else {
            item.push_back(c);
        }
    }
    if (!readItem()) return false;
    if (positions.empty()) {
        error = "enter at least one position: node:ID, element:ID or ip:ELEMENT:N, separated by ';'";
        return false;
    }
    return true;
}

} // namespace Fatigue
} // namespace OpenModel
