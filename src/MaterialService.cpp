/**
 * @file MaterialService.cpp
 * @brief Implementation of the Material tab logic (see MaterialService.hpp).
 */
#include <OpenModel/MaterialService.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

namespace OpenModel {

namespace {

std::string jsonEscapeText(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                    out += buffer;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string jsonNumber(double value) {
    std::ostringstream text;
    text.precision(12);
    text << value;
    return text.str();
}

std::string jsonOptional(const std::optional<double>& value, UnitSystem units, bool stress) {
    if (!value) return "null";
    const double shown = stress ? (units == UnitSystem::MmTonneS ? *value / 1.0e6 : *value) : *value;
    return jsonNumber(shown);
}

/** @brief Stress from the form to SI: the same factor as the Young modulus (MPa to Pa in mm-t-s). */
double stressFromForm(double value, UnitSystem units) {
    return youngModulusFromUnits(value, units);
}

/** Optional number: empty text is "not given"; any other text must be a finite number. */
bool optionalNumber(const std::string& text, const char* label, std::optional<double>& out, std::string& error) {
    if (text.empty()) {
        out.reset();
        return true;
    }
    double value = 0.0;
    if (!parseFiniteNumber(text, value)) {
        error = std::string(label) + " must be a number (leave it empty when it is not known)";
        return false;
    }
    out = value;
    return true;
}

/** Optional stress: as optionalNumber, converted from the form unit to SI. */
bool optionalStress(const std::string& text, const char* label, UnitSystem units, std::optional<double>& out,
                    std::string& error) {
    std::optional<double> value;
    if (!optionalNumber(text, label, value, error)) return false;
    out = value ? std::optional<double>(stressFromForm(*value, units)) : std::nullopt;
    return true;
}

std::string formatMaterialJson(const MaterialRecord& record, UnitSystem units) {
    const ElasticMaterial& m = record.elastic;
    const MaterialFatigueData& f = record.fatigue;
    std::ostringstream out;
    out << "{\"id\":\"" << jsonEscapeText(m.id) << "\",\"name\":\"" << jsonEscapeText(m.name)
        << "\",\"classKey\":\"" << materialClassKey(m.klass) << "\",\"className\":\""
        << jsonEscapeText(materialClassName(m.klass)) << "\",\"builtIn\":" << (record.builtIn ? "true" : "false")
        << ",\"E\":" << jsonNumber(youngModulusIn(m, units))
        << ",\"nu\":" << jsonNumber(m.poisson)
        << ",\"rho\":" << jsonNumber(densityIn(m, units))
        << ",\"note\":\"" << jsonEscapeText(m.note) << "\""
        << ",\"yieldStrength\":" << jsonOptional(f.yieldStrengthPa, units, true)
        << ",\"ultimateStrength\":" << jsonOptional(f.ultimateStrengthPa, units, true)
        << ",\"cyclicStrengthCoefficient\":" << jsonOptional(f.cyclicStrengthCoefficientPa, units, true)
        << ",\"cyclicStrainHardeningExponent\":" << jsonOptional(f.cyclicStrainHardeningExponent, units, false)
        << ",\"fatigueStrengthCoefficient\":" << jsonOptional(f.fatigueStrengthCoefficientPa, units, true)
        << ",\"fatigueStrengthExponent\":" << jsonOptional(f.fatigueStrengthExponent, units, false)
        << ",\"fatigueDuctilityCoefficient\":" << jsonOptional(f.fatigueDuctilityCoefficient, units, false)
        << ",\"fatigueDuctilityExponent\":" << jsonOptional(f.fatigueDuctilityExponent, units, false)
        << "}";
    return out.str();
}

} // namespace

bool parseFiniteNumber(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(out);
}

bool parseFormUnit(const std::string& text, UnitSystem& out) {
    return parseUnitSystem(text, out);
}

std::string recordFromForm(const MaterialForm& form, MaterialRecord& record) {
    UnitSystem units{};
    if (!parseFormUnit(form.unit, units)) return "Unknown unit system.";

    ElasticMaterial& m = record.elastic;
    m = ElasticMaterial{};
    m.id = form.id;
    m.name = form.name;
    if (!parseMaterialClass(form.classKey, m.klass)) return "Choose the family of the material.";

    double value = 0.0;
    if (!parseFiniteNumber(form.young, value)) return "The Young modulus E must be a number.";
    m.youngModulus = youngModulusFromUnits(value, units);
    if (!parseFiniteNumber(form.poisson, value)) return "The Poisson ratio nu must be a number.";
    m.poisson = value;
    if (!parseFiniteNumber(form.density, value)) return "The density rho must be a number.";
    m.density = densityFromUnits(value, units);
    m.note = form.note;

    MaterialFatigueData& f = record.fatigue;
    f = MaterialFatigueData{};
    std::string error;
    if (!optionalStress(form.yieldStrength, "The yield strength", units, f.yieldStrengthPa, error) ||
        !optionalStress(form.ultimateStrength, "The ultimate strength", units, f.ultimateStrengthPa, error) ||
        !optionalStress(form.cyclicStrengthCoefficient, "The cyclic strength coefficient K'", units,
                        f.cyclicStrengthCoefficientPa, error) ||
        !optionalNumber(form.cyclicStrainHardeningExponent, "The cyclic strain hardening exponent n'",
                        f.cyclicStrainHardeningExponent, error) ||
        !optionalStress(form.fatigueStrengthCoefficient, "The fatigue strength coefficient sigma_f'", units,
                        f.fatigueStrengthCoefficientPa, error) ||
        !optionalNumber(form.fatigueStrengthExponent, "The fatigue strength exponent b", f.fatigueStrengthExponent,
                        error) ||
        !optionalNumber(form.fatigueDuctilityCoefficient, "The fatigue ductility coefficient epsilon_f'",
                        f.fatigueDuctilityCoefficient, error) ||
        !optionalNumber(form.fatigueDuctilityExponent, "The fatigue ductility exponent c", f.fatigueDuctilityExponent,
                        error)) {
        return error;
    }
    record.builtIn = false;
    return validateMaterialRecord(record);
}

std::string materialListJson(const MaterialStore& store, UnitSystem units, const std::string& storePath,
                             const std::string& note) {
    std::ostringstream out;
    out << "{\"ok\":true,\"message\":\"" << jsonEscapeText(note) << "\",\"units\":\""
        << (units == UnitSystem::SI ? "si" : "mm-t-s") << "\",\"path\":\"" << jsonEscapeText(storePath)
        << "\",\"classes\":[";
    bool first = true;
    for (int k = 0; k <= static_cast<int>(MaterialClass::Stone); ++k) {
        const auto klass = static_cast<MaterialClass>(k);
        out << (first ? "" : ",") << "{\"key\":\"" << materialClassKey(klass) << "\",\"name\":\""
            << jsonEscapeText(materialClassName(klass)) << "\"}";
        first = false;
    }
    out << "],\"materials\":[";
    first = true;
    for (const MaterialRecord& record : store.list()) {
        out << (first ? "" : ",") << formatMaterialJson(record, units);
        first = false;
    }
    out << "]}";
    return out.str();
}

MaterialServiceReply saveMaterialForm(MaterialStore& store, const MaterialForm& form) {
    MaterialServiceReply reply;
    if (!store.isOpen()) {
        reply.message = "The material store is not open.";
        return reply;
    }
    MaterialRecord record;
    if (std::string reason = recordFromForm(form, record); !reason.empty()) {
        reply.message = reason;
        return reply;
    }
    std::string error;
    const bool exists = store.find(record.elastic.id).has_value();
    const bool ok = exists ? store.update(record.elastic.id, record, error) : store.add(record, error);
    if (!ok) {
        reply.message = error;
        return reply;
    }
    reply.ok = true;
    reply.message = "Known material '" + record.elastic.id + "' saved.";
    return reply;
}

MaterialServiceReply deleteMaterialById(MaterialStore& store, const std::string& id) {
    MaterialServiceReply reply;
    if (!store.isOpen()) {
        reply.message = "The material store is not open.";
        return reply;
    }
    std::string error;
    if (!store.remove(id, error)) {
        reply.message = error;
        return reply;
    }
    reply.ok = true;
    reply.message = "Known material '" + id + "' removed.";
    return reply;
}

std::string importLegacyLibraryOnce(MaterialStore& store, const std::string& legacyTsvPath, bool storeWasCreated) {
    if (!storeWasCreated || !store.isOpen()) return {};
    std::error_code ec;
    if (!std::filesystem::exists(legacyTsvPath, ec)) return {};
    std::vector<std::string> problems;
    std::string error;
    if (!store.importTsv(legacyTsvPath, problems, error)) {
        return "Cannot import " + legacyTsvPath + ": " + error;
    }
    // The old file is renamed so that it is never imported again: materials.tsv becomes materials.tsv.old.
    const std::string renamed = legacyTsvPath + ".old";
    std::filesystem::rename(legacyTsvPath, renamed, ec);
    if (ec) problems.push_back("the imported file could not be renamed to " + renamed + ": " + ec.message());
    std::string joined;
    for (std::size_t k = 0; k < problems.size(); ++k) {
        joined += (k ? "; " : "") + problems[k];
    }
    return joined;
}

} // namespace OpenModel
