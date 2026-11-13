#include <OpenModel/Fatigue/FatigueSpecification.hpp>

#include <cmath>
#include <cstdint>
#include <locale>
#include <sstream>

/**
 * @file FatigueSpecification.cpp
 * @brief Readers of the fatigue material, position and spectrum files. Errors name the JSON path that failed.
 */

namespace OpenModel {
namespace Fatigue {

namespace {

using Json::Value;

SpecificationResult success() { return SpecificationResult{true, std::string()}; }

SpecificationResult failure(const std::string& path, const std::string& message) {
    return SpecificationResult{false, path.empty() ? message : path + ": " + message};
}

std::string join(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
}

std::string indexed(const std::string& path, const char* name, std::size_t index) {
    return join(path, name) + "[" + std::to_string(index) + "]";
}

/** Optional number member: keeps `out` when absent. */
SpecificationResult optionalNumber(const Value& object, const std::string& path, const char* key, double& out) {
    const Value* member = object.find(key);
    if (!member) return success();
    if (!member->isNumber()) return failure(join(path, key), "expected a number");
    out = member->asNumber();
    return success();
}

SpecificationResult requiredNumber(const Value& object, const std::string& path, const char* key, double& out) {
    const Value* member = object.find(key);
    if (!member) return failure(path, std::string("missing '") + key + "'");
    if (!member->isNumber()) return failure(join(path, key), "expected a number");
    out = member->asNumber();
    return success();
}

SpecificationResult optionalString(const Value& object, const std::string& path, const char* key, std::string& out) {
    const Value* member = object.find(key);
    if (!member) return success();
    if (!member->isString()) return failure(join(path, key), "expected a string");
    out = member->asString();
    return success();
}

SpecificationResult requiredString(const Value& object, const std::string& path, const char* key, std::string& out) {
    const Value* member = object.find(key);
    if (!member) return failure(path, std::string("missing '") + key + "'");
    if (!member->isString()) return failure(join(path, key), "expected a string");
    if (member->asString().empty()) return failure(join(path, key), "must not be empty");
    out = member->asString();
    return success();
}

/** Parses a non-negative decimal integer completely; rejects signs, blanks and overflow. */
bool parseUnsigned(const std::string& text, std::uint64_t& out) {
    if (text.empty()) return false;
    std::uint64_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (value > (UINT64_MAX - digit) / 10) return false;
        value = value * 10 + digit;
    }
    out = value;
    return true;
}

bool parseDouble(const std::string& text, double& out) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0.0;
    stream >> value;
    if (stream.fail() || !std::isfinite(value)) return false;
    stream >> std::ws;
    if (!stream.eof()) return false;
    out = value;
    return true;
}

SpecificationResult readUnit(const Value& object, const std::string& path, PhysicalUnit& unit) {
    if (!object.isObject()) return failure(path, "expected an object");
    if (auto r = requiredString(object, path, "symbol", unit.symbol); !r) return r;
    if (auto r = requiredNumber(object, path, "scaleToSI", unit.scaleToSI); !r) return r;
    if (auto r = optionalNumber(object, path, "offsetToSI", unit.offsetToSI); !r) return r;

    const Value* dimension = object.find("dimension");
    if (dimension) {
        const std::string dimensionPath = join(path, "dimension");
        if (!dimension->isObject()) return failure(dimensionPath, "expected an object of SI exponents");
        unit.dimension.known = true;
        struct Exponent { const char* key; std::int8_t* target; };
        const Exponent exponents[] = {
            {"length", &unit.dimension.length}, {"mass", &unit.dimension.mass}, {"time", &unit.dimension.time},
            {"current", &unit.dimension.current}, {"temperature", &unit.dimension.temperature},
            {"amount", &unit.dimension.amount}, {"luminousIntensity", &unit.dimension.luminousIntensity}};
        for (const auto& e : exponents) {
            double value = 0.0;
            if (const Value* member = dimension->find(e.key)) {
                if (!member->isNumber()) return failure(join(dimensionPath, e.key), "expected an integer exponent");
                value = member->asNumber();
            }
            if (value != std::floor(value) || std::fabs(value) > 8.0) {
                return failure(join(dimensionPath, e.key), "expected an integer exponent between -8 and 8");
            }
            *e.target = static_cast<std::int8_t>(value);
        }
        for (const auto& key : dimension->keys()) {
            bool known = false;
            for (const auto& e : exponents) known = known || key == e.key;
            if (!known) return failure(dimensionPath, "unknown exponent '" + key + "'");
        }
    }
    return success();
}

SpecificationResult readPoints(const Value& array, const std::string& path, std::vector<FatigueCurvePoint>& points) {
    if (!array.isArray()) return failure(path, "expected an array of points");
    if (array.asArray().empty()) return failure(path, "must contain at least one point");
    for (std::size_t i = 0; i < array.asArray().size(); ++i) {
        const Value& item = array.asArray()[i];
        const std::string itemPath = indexed(path, "points", i);
        if (!item.isObject()) return failure(itemPath, "expected an object with amplitude and cyclesToFailure");
        FatigueCurvePoint point;
        if (auto r = requiredNumber(item, itemPath, "amplitude", point.amplitude); !r) return r;
        if (auto r = requiredNumber(item, itemPath, "cyclesToFailure", point.cyclesToFailure); !r) return r;
        points.push_back(point);
    }
    return success();
}

/** Reads `[x, y, z]` into a unit direction. */
SpecificationResult readProjectionArray(const Value& array, const std::string& path, StressProjection& projection) {
    if (!array.isArray() || array.asArray().size() != 3) return failure(path, "expected an array of three numbers");
    std::array<double, 3> v{};
    for (std::size_t i = 0; i < 3; ++i) {
        if (!array.asArray()[i].isNumber()) return failure(path, "expected three numbers");
        v[i] = array.asArray()[i].asNumber();
    }
    const double norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!std::isfinite(norm) || norm <= 0.0) return failure(path, "the direction must be a non-zero vector");
    projection.direction = {{v[0] / norm, v[1] / norm, v[2] / norm}};
    return success();
}

} // namespace

SpecificationResult readMaterial(const Value& root, FatigueMaterial& material) {
    if (!root.isObject()) return failure("", "the material must be a JSON object");
    FatigueMaterial result;
    if (auto r = optionalString(root, "", "name", result.name); !r) return r;
    if (auto r = optionalString(root, "", "materialId", result.materialId); !r) return r;
    if (auto r = optionalString(root, "", "source", result.source); !r) return r;
    if (auto r = optionalString(root, "", "notes", result.notes); !r) return r;

    const Value* curves = root.find("curves");
    if (!curves) return failure("", "missing 'curves'");
    if (!curves->isArray() || curves->asArray().empty()) return failure("curves", "expected a non-empty array");

    for (std::size_t c = 0; c < curves->asArray().size(); ++c) {
        const Value& curveValue = curves->asArray()[c];
        const std::string curvePath = indexed("", "curves", c);
        if (!curveValue.isObject()) return failure(curvePath, "expected an object");

        std::string kindText;
        if (auto r = requiredString(curveValue, curvePath, "kind", kindText); !r) return r;
        FatigueCurveKind kind;
        if (kindText == "stressLifeSN") kind = FatigueCurveKind::StressLifeSN;
        else if (kindText == "strainLifeEN") kind = FatigueCurveKind::StrainLifeEN;
        else return failure(join(curvePath, "kind"), "unknown kind '" + kindText + "' (expected stressLifeSN or strainLifeEN)");

        const Value* unitValue = curveValue.find("amplitudeUnit");
        if (!unitValue) return failure(curvePath, "missing 'amplitudeUnit'");
        PhysicalUnit unit;
        if (auto r = readUnit(*unitValue, join(curvePath, "amplitudeUnit"), unit); !r) return r;

        const Value* slices = curveValue.find("slices");
        if (!slices) return failure(curvePath, "missing 'slices'");
        if (!slices->isArray() || slices->asArray().empty()) return failure(join(curvePath, "slices"), "expected a non-empty array");

        std::vector<FatigueCurveSlice> sliceList;
        for (std::size_t s = 0; s < slices->asArray().size(); ++s) {
            const Value& sliceValue = slices->asArray()[s];
            const std::string slicePath = indexed(curvePath, "slices", s);
            if (!sliceValue.isObject()) return failure(slicePath, "expected an object");
            FatigueCurveSlice slice;
            if (auto r = requiredNumber(sliceValue, slicePath, "temperatureKelvin", slice.temperatureKelvin); !r) return r;
            const Value* points = sliceValue.find("points");
            if (!points) return failure(slicePath, "missing 'points'");
            if (auto r = readPoints(*points, join(slicePath, "points"), slice.points); !r) return r;
            sliceList.push_back(std::move(slice));
        }

        FatigueCurve curve;
        std::string diagnostic;
        if (!FatigueCurve::create(kind, unit, std::move(sliceList), curve, diagnostic)) {
            return failure(curvePath, diagnostic.empty() ? "invalid curve" : diagnostic);
        }
        result.curves.push_back(std::move(curve));
    }

    std::string diagnostic;
    if (result.validate(diagnostic) != FatigueCurveStatus::Success) {
        return failure("", diagnostic.empty() ? "invalid material" : diagnostic);
    }
    material = std::move(result);
    return success();
}

SpecificationResult parsePosition(const std::string& text, ResultPosition& position) {
    const std::string bad = "expected node:ID, element:ID or ip:ELEMENT:N, got '" + text + "'";
    const auto colon = text.find(':');
    if (colon == std::string::npos) return failure("", bad);
    const std::string kind = text.substr(0, colon);
    const std::string rest = text.substr(colon + 1);

    ResultPosition parsed;
    if (kind == "node" || kind == "element") {
        std::uint64_t id = 0;
        if (!parseUnsigned(rest, id) || id == 0) return failure("", bad);
        parsed.location = kind == "node" ? ResultLocation::Node : ResultLocation::Element;
        parsed.entityId = id;
    } else if (kind == "ip") {
        const auto second = rest.find(':');
        if (second == std::string::npos) return failure("", bad);
        std::uint64_t element = 0, point = 0;
        if (!parseUnsigned(rest.substr(0, second), element) || element == 0) return failure("", bad);
        if (!parseUnsigned(rest.substr(second + 1), point) || point == 0 || point > UINT32_MAX) return failure("", bad);
        parsed.location = ResultLocation::IntegrationPoint;
        parsed.entityId = element;
        parsed.integrationPoint = static_cast<std::uint32_t>(point);
    } else {
        return failure("", bad);
    }
    position = parsed;
    return success();
}

SpecificationResult parseProjection(const std::string& text, StressProjection& projection) {
    const std::string bad = "expected x,y,z, got '" + text + "'";
    std::array<double, 3> v{};
    std::size_t start = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto comma = text.find(',', start);
        const bool last = i == 2;
        if ((last && comma != std::string::npos) || (!last && comma == std::string::npos)) return failure("", bad);
        const std::string part = text.substr(start, last ? std::string::npos : comma - start);
        if (!parseDouble(part, v[i])) return failure("", bad);
        start = comma + 1;
    }
    const double norm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!std::isfinite(norm) || norm <= 0.0) return failure("", "the direction must be a non-zero vector");
    projection.direction = {{v[0] / norm, v[1] / norm, v[2] / norm}};
    return success();
}

SpecificationResult parseUnitDeclaration(const std::string& text, PhysicalUnit& unit) {
    const std::string bad = "expected SYMBOL:SCALE (for example MPa:1e6), got '" + text + "'";
    const auto colon = text.rfind(':');
    if (colon == std::string::npos || colon == 0) return failure("", bad);
    double scale = 0.0;
    if (!parseDouble(text.substr(colon + 1), scale) || !(scale > 0.0)) return failure("", bad);
    PhysicalUnit declared;
    declared.symbol = text.substr(0, colon);
    declared.scaleToSI = scale;
    unit = declared;
    return success();
}

bool declareFieldUnit(FemResults& results, const std::string& fieldName, const PhysicalUnit& unit, std::string& error) {
    bool found = false;
    for (auto& field : results.fields) {
        if (field.name != fieldName) continue;
        found = true;
        field.semantics.unit.symbol = unit.symbol;
        field.semantics.unit.scaleToSI = unit.scaleToSI;
        field.semantics.unit.offsetToSI = unit.offsetToSI;
    }
    if (!found) error = "no field named '" + fieldName + "' in the results";
    return found;
}

SpecificationResult readSpectrum(const Value& root, SpectrumSpecification& spectrum) {
    if (!root.isObject()) return failure("", "the spectrum must be a JSON object");
    SpectrumSpecification result;
    if (auto r = optionalNumber(root, "", "temperatureKelvin", result.temperatureKelvin); !r) return r;
    if (!(result.temperatureKelvin > 0.0)) return failure("temperatureKelvin", "must be positive");

    const Value* positions = root.find("positions");
    if (!positions) return failure("", "missing 'positions'");
    if (!positions->isArray() || positions->asArray().empty()) return failure("positions", "expected a non-empty array");

    for (std::size_t p = 0; p < positions->asArray().size(); ++p) {
        const Value& positionValue = positions->asArray()[p];
        const std::string positionPath = indexed("", "positions", p);
        if (!positionValue.isObject()) return failure(positionPath, "expected an object");

        SpectrumPositionSpecification entry;
        std::string positionText;
        if (auto r = requiredString(positionValue, positionPath, "position", positionText); !r) return r;
        if (auto r = parsePosition(positionText, entry.position); !r) return failure(join(positionPath, "position"), r.error);
        entry.label = positionText;
        if (auto r = optionalString(positionValue, positionPath, "label", entry.label); !r) return r;

        const Value* blocks = positionValue.find("blocks");
        if (!blocks) return failure(positionPath, "missing 'blocks'");
        if (!blocks->isArray() || blocks->asArray().empty()) return failure(join(positionPath, "blocks"), "expected a non-empty array");

        for (std::size_t b = 0; b < blocks->asArray().size(); ++b) {
            const Value& blockValue = blocks->asArray()[b];
            const std::string blockPath = indexed(positionPath, "blocks", b);
            if (!blockValue.isObject()) return failure(blockPath, "expected an object");

            SpectrumBlockSpecification block;
            block.label = "block " + std::to_string(b + 1);
            if (auto r = optionalString(blockValue, blockPath, "label", block.label); !r) return r;
            if (auto r = optionalNumber(blockValue, blockPath, "repetitions", block.repetitions); !r) return r;
            if (!(block.repetitions > 0.0)) return failure(join(blockPath, "repetitions"), "must be positive");
            if (auto r = requiredString(blockValue, blockPath, "field", block.fieldName); !r) return r;

            const Value* results = blockValue.find("results");
            if (!results) return failure(blockPath, "missing 'results'");
            if (!results->isArray() || results->asArray().empty()) return failure(join(blockPath, "results"), "expected a non-empty array of file names");
            for (std::size_t f = 0; f < results->asArray().size(); ++f) {
                const Value& file = results->asArray()[f];
                const std::string filePath = indexed(blockPath, "results", f);
                if (!file.isString() || file.asString().empty()) return failure(filePath, "expected a file name");
                block.results.push_back(file.asString());
            }

            block.projection = StressProjection();
            if (const Value* projection = blockValue.find("projection")) {
                if (auto r = readProjectionArray(*projection, join(blockPath, "projection"), block.projection); !r) return r;
            }
            if (const Value* resultUnit = blockValue.find("resultUnit")) {
                const std::string unitPath = join(blockPath, "resultUnit");
                if (!resultUnit->isObject()) return failure(unitPath, "expected an object with symbol and scaleToSI");
                if (resultUnit->find("dimension")) return failure(unitPath, "the dimension comes from the results; remove 'dimension'");
                if (auto r = readUnit(*resultUnit, unitPath, block.resultUnit); !r) return r;
                block.hasResultUnit = true;
            }
            entry.blocks.push_back(std::move(block));
        }
        result.positions.push_back(std::move(entry));
    }
    spectrum = std::move(result);
    return success();
}

} // namespace Fatigue
} // namespace OpenModel
