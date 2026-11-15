/**
 * @file MaterialDatabase.cpp
 * @brief Built-in database of linear elastic materials; see MaterialDatabase.hpp.
 */
#include <OpenModel/MaterialDatabase.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace OpenModel {

namespace {

/** Converts a database value (SI) to the requested unit system. */
constexpr double kPaToMPa = 1.0e-6;          ///< 1 Pa = 1e-6 MPa.
constexpr double kKgM3ToTonneMm3 = 1.0e-12;  ///< 1 kg/m^3 = 1e-3 t / 1e9 mm^3.

const std::vector<ElasticMaterial>& buildDatabase() {
    static const std::vector<ElasticMaterial> db = {
        // Aluminium alloys, typical values (E about 69 to 73 GPa, nu 0.33).
        {"al_1050", "Aluminium 1050 (commercially pure)", MaterialClass::Aluminium, 69.0e9, 0.33, 2710.0,
         "Typical value for the annealed condition."},
        {"al_6061_t6", "Aluminium 6061-T6", MaterialClass::Aluminium, 68.9e9, 0.33, 2700.0,
         "Typical value for the T6 temper."},
        {"al_2024_t3", "Aluminium 2024-T3", MaterialClass::Aluminium, 73.1e9, 0.33, 2780.0,
         "Typical value for the T3 temper."},
        {"al_7075_t6", "Aluminium 7075-T6", MaterialClass::Aluminium, 71.7e9, 0.33, 2810.0,
         "Typical value for the T6 temper."},

        // Steels and cast iron.
        {"steel_s235", "Structural steel S235", MaterialClass::SteelAndIron, 210.0e9, 0.30, 7850.0,
         "Elastic values as in EN 1993-1-1 (E = 210 GPa, nu = 0.3)."},
        {"steel_s275", "Structural steel S275", MaterialClass::SteelAndIron, 210.0e9, 0.30, 7850.0,
         "Elastic values as in EN 1993-1-1 (E = 210 GPa, nu = 0.3)."},
        {"steel_s355", "Structural steel S355", MaterialClass::SteelAndIron, 210.0e9, 0.30, 7850.0,
         "Elastic values as in EN 1993-1-1 (E = 210 GPa, nu = 0.3)."},
        {"steel_a36", "Structural steel ASTM A36", MaterialClass::SteelAndIron, 200.0e9, 0.26, 7850.0,
         "Typical value for ASTM A36 (E = 200 GPa)."},
        {"steel_aisi304", "Stainless steel AISI 304", MaterialClass::SteelAndIron, 193.0e9, 0.29, 8000.0,
         "Typical value for austenitic stainless steel at room temperature."},
        {"steel_aisi316", "Stainless steel AISI 316", MaterialClass::SteelAndIron, 193.0e9, 0.30, 8000.0,
         "Typical value for austenitic stainless steel at room temperature."},
        {"cast_iron_grey", "Grey cast iron", MaterialClass::SteelAndIron, 110.0e9, 0.26, 7200.0,
         "Indicative: the modulus of grey cast iron varies with the grade and the graphite form."},

        // Other metals.
        {"copper", "Copper (annealed)", MaterialClass::OtherMetal, 117.0e9, 0.34, 8960.0,
         "Typical value for annealed copper."},
        {"brass_cuzn37", "Brass CuZn37", MaterialClass::OtherMetal, 103.0e9, 0.34, 8500.0,
         "Indicative value for a cartridge brass."},
        {"ti_6al_4v", "Titanium alloy Ti-6Al-4V", MaterialClass::OtherMetal, 113.8e9, 0.34, 4430.0,
         "Typical value for the annealed alloy."},
        {"mg_az31b", "Magnesium alloy AZ31B", MaterialClass::OtherMetal, 45.0e9, 0.35, 1770.0,
         "Typical value for the wrought alloy."},

        // Concrete: secant modulus Ecm of EN 1992-1-1 Table 3.1, plain concrete density.
        {"concrete_c25_30", "Concrete C25/30", MaterialClass::Concrete, 31.0e9, 0.20, 2400.0,
         "Ecm from EN 1992-1-1 Table 3.1. Plain concrete: reinforced concrete is about 2500 kg/m^3."},
        {"concrete_c30_37", "Concrete C30/37", MaterialClass::Concrete, 33.0e9, 0.20, 2400.0,
         "Ecm from EN 1992-1-1 Table 3.1. Plain concrete: reinforced concrete is about 2500 kg/m^3."},
        {"concrete_c35_45", "Concrete C35/45", MaterialClass::Concrete, 34.0e9, 0.20, 2400.0,
         "Ecm from EN 1992-1-1 Table 3.1. Plain concrete: reinforced concrete is about 2500 kg/m^3."},
        {"concrete_c40_50", "Concrete C40/50", MaterialClass::Concrete, 35.0e9, 0.20, 2400.0,
         "Ecm from EN 1992-1-1 Table 3.1. Plain concrete: reinforced concrete is about 2500 kg/m^3."},
        {"concrete_c50_60", "Concrete C50/60", MaterialClass::Concrete, 37.0e9, 0.20, 2400.0,
         "Ecm from EN 1992-1-1 Table 3.1. Plain concrete: reinforced concrete is about 2500 kg/m^3."},

        // Wood: orthotropic in reality. The values are the longitudinal (grain) modulus only.
        {"wood_spruce_l", "Spruce, grain direction", MaterialClass::Wood, 11.0e9, 0.30, 450.0,
         "Longitudinal modulus only. The transverse modulus is much smaller: use only for models loaded along the grain."},
        {"wood_oak_l", "Oak, grain direction", MaterialClass::Wood, 12.0e9, 0.30, 700.0,
         "Longitudinal modulus only, rough value. The transverse modulus is much smaller."},

        // Polymers, indicative: the modulus depends on temperature, humidity and grade.
        {"abs", "ABS", MaterialClass::Polymer, 2.3e9, 0.35, 1050.0,
         "Indicative value for unfilled ABS."},
        {"pa6_dry", "Polyamide PA6 (dry)", MaterialClass::Polymer, 2.7e9, 0.40, 1140.0,
         "Indicative value for dry PA6; the modulus drops with absorbed moisture."},
        {"pmma", "PMMA (acrylic)", MaterialClass::Polymer, 3.2e9, 0.37, 1180.0,
         "Indicative value for cast PMMA."},
        {"hdpe", "HDPE", MaterialClass::Polymer, 0.9e9, 0.42, 950.0,
         "Indicative value; the modulus depends strongly on the time of load."},
        {"epoxy", "Epoxy resin (unfilled)", MaterialClass::Polymer, 3.0e9, 0.37, 1200.0,
         "Indicative value for an unfilled cured resin."},

        // Glass and technical ceramics.
        {"glass_soda_lime", "Soda-lime glass", MaterialClass::GlassCeramic, 70.0e9, 0.22, 2500.0,
         "Typical value for float glass."},
        {"alumina_dense", "Alumina (Al2O3, dense)", MaterialClass::GlassCeramic, 370.0e9, 0.22, 3900.0,
         "Typical value for dense sintered alumina."},

        // Natural stone: large scatter between rock types.
        {"granite", "Granite", MaterialClass::Stone, 50.0e9, 0.25, 2700.0,
         "Indicative mid-range value: granites vary roughly from 40 to 70 GPa."},
    };
    return db;
}

} // namespace

const std::vector<ElasticMaterial>& materialDatabase() {
    return buildDatabase();
}

const ElasticMaterial* findMaterial(std::string_view id) {
    for (const ElasticMaterial& material : materialDatabase()) {
        if (material.id == id) return &material;
    }
    return nullptr;
}

const char* materialClassName(MaterialClass klass) {
    switch (klass) {
        case MaterialClass::Aluminium: return "Aluminium";
        case MaterialClass::SteelAndIron: return "Steel and cast iron";
        case MaterialClass::OtherMetal: return "Other metals";
        case MaterialClass::Concrete: return "Concrete";
        case MaterialClass::Wood: return "Wood";
        case MaterialClass::Polymer: return "Polymers";
        case MaterialClass::GlassCeramic: return "Glass and ceramics";
        case MaterialClass::Stone: return "Natural stone";
    }
    return "";
}

bool parseUnitSystem(std::string_view text, UnitSystem& out) {
    if (text == "si") { out = UnitSystem::SI; return true; }
    if (text == "mm-t-s") { out = UnitSystem::MmTonneS; return true; }
    return false;
}

double youngModulusIn(const ElasticMaterial& material, UnitSystem units) {
    return units == UnitSystem::MmTonneS ? material.youngModulus * kPaToMPa : material.youngModulus;
}

double densityIn(const ElasticMaterial& material, UnitSystem units) {
    return units == UnitSystem::MmTonneS ? material.density * kKgM3ToTonneMm3 : material.density;
}

std::string materialScriptLine(const ElasticMaterial& material, UnitSystem units) {
    std::ostringstream line;
    line.precision(12);
    line << "material E=" << youngModulusIn(material, units) << " nu=" << material.poisson
         << " rho=" << densityIn(material, units) << " name=" << material.id;
    return line.str();
}

} // namespace OpenModel

namespace OpenModel {

namespace {

constexpr const char* kLibraryHeader = "# OpenModel materials: id, name, class, E [Pa], nu, rho [kg/m^3], note";

struct ClassKey {
    MaterialClass klass;
    const char* key;
};

const ClassKey kClassKeys[] = {
    {MaterialClass::Aluminium, "aluminium"},
    {MaterialClass::SteelAndIron, "steel"},
    {MaterialClass::OtherMetal, "other_metal"},
    {MaterialClass::Concrete, "concrete"},
    {MaterialClass::Wood, "wood"},
    {MaterialClass::Polymer, "polymer"},
    {MaterialClass::GlassCeramic, "glass_ceramic"},
    {MaterialClass::Stone, "stone"},
};

bool validId(std::string_view id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
    return true;
}

bool plainText(std::string_view text, std::size_t maxLength) {
    if (text.size() > maxLength) return false;
    for (const char c : text) {
        if (c == '\t' || c == '\n' || c == '\r') return false;
    }
    return true;
}

bool finitePositive(double value) { return std::isfinite(value) && value > 0.0; }

/** Strict number parse: the whole field must be a number. */
bool parseNumber(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(out);
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    std::istringstream in(line);
    while (std::getline(in, field, '\t')) fields.push_back(field);
    if (!line.empty() && line.back() == '\t') fields.emplace_back();
    return fields;
}

} // namespace

const char* materialClassKey(MaterialClass klass) {
    for (const auto& k : kClassKeys) {
        if (k.klass == klass) return k.key;
    }
    return "";
}

bool parseMaterialClass(std::string_view key, MaterialClass& out) {
    for (const auto& k : kClassKeys) {
        if (key == k.key) { out = k.klass; return true; }
    }
    return false;
}

double youngModulusFromUnits(double value, UnitSystem units) {
    return units == UnitSystem::MmTonneS ? value * 1.0e6 : value;
}

double densityFromUnits(double value, UnitSystem units) {
    return units == UnitSystem::MmTonneS ? value * 1.0e12 : value;
}

std::string validateMaterial(const ElasticMaterial& material) {
    if (!validId(material.id)) return "The identifier must use lower case letters, digits, '_', '-' or '.' (at most 64 characters).";
    if (material.name.empty() || !plainText(material.name, 120)) return "The name is required (at most 120 characters, no tabs or line breaks).";
    if (!plainText(material.note, 300)) return "The note must not contain tabs or line breaks (at most 300 characters).";
    if (!finitePositive(material.youngModulus)) return "The Young modulus must be a positive number.";
    if (!std::isfinite(material.poisson) || material.poisson < 0.0 || material.poisson >= 0.5) return "The Poisson ratio must be in [0, 0.5).";
    if (!finitePositive(material.density)) return "The density must be a positive number.";
    return {};
}

MaterialLibrary::MaterialLibrary() : m_entries(materialDatabase()), m_builtInCount(m_entries.size()) {}

const ElasticMaterial* MaterialLibrary::find(std::string_view id) const {
    for (const ElasticMaterial& material : m_entries) {
        if (material.id == id) return &material;
    }
    return nullptr;
}

bool MaterialLibrary::isBuiltIn(std::string_view id) const {
    for (std::size_t i = 0; i < m_builtInCount; ++i) {
        if (m_entries[i].id == id) return true;
    }
    return false;
}

bool MaterialLibrary::add(const ElasticMaterial& material, std::string& error) {
    error = validateMaterial(material);
    if (!error.empty()) return false;
    if (find(material.id)) {
        error = isBuiltIn(material.id) ? "The identifier '" + material.id + "' belongs to a built-in material: choose another identifier."
                                       : "The identifier '" + material.id + "' is already used.";
        return false;
    }
    m_entries.push_back(material);
    return true;
}

bool MaterialLibrary::update(std::string_view id, const ElasticMaterial& material, std::string& error) {
    if (isBuiltIn(id)) { error = "Built-in materials cannot be changed: save a copy with another identifier."; return false; }
    if (material.id != id) { error = "The identifier of a known material cannot be changed."; return false; }
    error = validateMaterial(material);
    if (!error.empty()) return false;
    for (ElasticMaterial& existing : m_entries) {
        if (existing.id == id) { existing = material; return true; }
    }
    error = "No known material with identifier '" + std::string(id) + "'.";
    return false;
}

bool MaterialLibrary::remove(std::string_view id, std::string& error) {
    if (isBuiltIn(id)) { error = "Built-in materials cannot be removed."; return false; }
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->id == id) { m_entries.erase(it); return true; }
    }
    error = "No known material with identifier '" + std::string(id) + "'.";
    return false;
}

bool MaterialLibrary::load(const std::string& path, std::vector<std::string>& problems, std::string& error) {
    error.clear();
    problems.clear();
    std::ifstream in(path);
    if (!in) return true;  // no file: no custom entries yet
    std::string line;
    std::size_t number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const auto fields = splitTabs(line);
        auto fail = [&](const std::string& reason) {
            problems.push_back("line " + std::to_string(number) + ": " + reason);
        };
        if (fields.size() != 7) { fail("expected 7 tab-separated fields"); continue; }
        ElasticMaterial material;
        material.id = fields[0];
        material.name = fields[1];
        material.note = fields[6];
        if (!parseMaterialClass(fields[2], material.klass)) { fail("unknown class '" + fields[2] + "'"); continue; }
        if (!parseNumber(fields[3], material.youngModulus) || !parseNumber(fields[4], material.poisson) ||
            !parseNumber(fields[5], material.density)) {
            fail("E, nu and rho must be numbers");
            continue;
        }
        const std::string reason = validateMaterial(material);
        if (!reason.empty()) { fail(reason); continue; }
        if (find(material.id)) { fail("identifier '" + material.id + "' is already used"); continue; }
        m_entries.push_back(material);
    }
    return true;
}

bool MaterialLibrary::save(const std::string& path, std::string& error) const {
    namespace fs = std::filesystem;
    error.clear();
    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path temporary = target.string() + ".tmp";
    {
        std::ofstream out(temporary, std::ios::trunc);
        if (!out) { error = "Cannot write " + temporary.string(); return false; }
        out << kLibraryHeader << "\n";
        out.precision(17);
        for (std::size_t i = m_builtInCount; i < m_entries.size(); ++i) {
            const ElasticMaterial& m = m_entries[i];
            out << m.id << '\t' << m.name << '\t' << materialClassKey(m.klass) << '\t' << m.youngModulus << '\t'
                << m.poisson << '\t' << m.density << '\t' << m.note << '\n';
        }
        if (!out) { error = "Writing " + temporary.string() + " failed."; return false; }
    }
    fs::rename(temporary, target, ec);
    if (ec) { error = "Cannot replace " + target.string() + ": " + ec.message(); return false; }
    return true;
}

} // namespace OpenModel
