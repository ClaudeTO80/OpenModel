/**
 * @file MaterialDatabase.cpp
 * @brief Built-in database of linear elastic materials; see MaterialDatabase.hpp.
 */
#include <OpenModel/MaterialDatabase.hpp>

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
