/**
 * @file material_service_test.cpp
 * @brief Checks the Material tab logic: form parsing and unit conversion, JSON listing, save/delete rules
 * and the first-run import of the legacy library.
 */
#include <OpenModel/MaterialService.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool near(double a, double b) {
    return std::abs(a - b) <= 1e-9 * std::max(1.0, std::abs(b));
}

/** A complete steel form in mm-t-s: E in MPa, density in t/mm^3, stresses in MPa. */
OpenModel::MaterialForm steelFormMmTS(const std::string& id) {
    OpenModel::MaterialForm f;
    f.unit = "mm-t-s";
    f.id = id;
    f.name = "Test steel " + id;
    f.classKey = "steel";
    f.young = "210000";
    f.poisson = "0.3";
    f.density = "7.85e-9";
    f.note = "Test values, not certified.";
    f.yieldStrength = "355";
    f.ultimateStrength = "470";
    f.cyclicStrengthCoefficient = "1000";
    f.cyclicStrainHardeningExponent = "0.2";
    f.fatigueStrengthCoefficient = "900";
    f.fatigueStrengthExponent = "-0.09";
    f.fatigueDuctilityCoefficient = "0.5";
    f.fatigueDuctilityExponent = "-0.6";
    return f;
}

std::filesystem::path tempFile(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    std::filesystem::remove(std::filesystem::path(path.string() + "-journal"));
    return path;
}

OpenModel::MaterialStore openStore(const std::filesystem::path& path) {
    OpenModel::MaterialStore store;
    std::string error;
    if (!OpenModel::MaterialStore::open(path.string(), store, error)) {
        std::cerr << "cannot open test store: " << error << "\n";
        ++g_failures;
    }
    return store;
}

void testFormConversion() {
    OpenModel::MaterialRecord record;

    // mm-t-s: stresses in MPa, density in t/mm^3, both converted to SI.
    CHECK(OpenModel::recordFromForm(steelFormMmTS("steel_a"), record).empty());
    CHECK(near(record.elastic.youngModulus, 210.0e9));
    CHECK(near(record.elastic.density, 7850.0));
    CHECK(record.fatigue.yieldStrengthPa && near(*record.fatigue.yieldStrengthPa, 355.0e6));
    CHECK(record.fatigue.cyclicStrengthCoefficientPa && near(*record.fatigue.cyclicStrengthCoefficientPa, 1000.0e6));
    CHECK(record.fatigue.fatigueStrengthExponent && near(*record.fatigue.fatigueStrengthExponent, -0.09));
    CHECK(!record.builtIn);

    // SI: the same material entered in SI gives the same stored values.
    OpenModel::MaterialForm si = steelFormMmTS("steel_b");
    si.unit = "si";
    si.young = "210e9";
    si.density = "7850";
    si.yieldStrength = "355e6";
    si.ultimateStrength = "470e6";
    si.cyclicStrengthCoefficient = "1000e6";
    si.fatigueStrengthCoefficient = "900e6";
    OpenModel::MaterialRecord siRecord;
    CHECK(OpenModel::recordFromForm(si, siRecord).empty());
    CHECK(near(siRecord.elastic.youngModulus, record.elastic.youngModulus));
    CHECK(near(siRecord.elastic.density, record.elastic.density));
    CHECK(siRecord.fatigue.yieldStrengthPa && near(*siRecord.fatigue.yieldStrengthPa, 355.0e6));
}

void testOptionalFieldsAndErrors() {
    OpenModel::MaterialRecord record;

    // Empty fatigue fields are absent, not zero.
    OpenModel::MaterialForm elasticOnly = steelFormMmTS("steel_c");
    elasticOnly.yieldStrength.clear();
    elasticOnly.ultimateStrength.clear();
    elasticOnly.cyclicStrengthCoefficient.clear();
    elasticOnly.cyclicStrainHardeningExponent.clear();
    elasticOnly.fatigueStrengthCoefficient.clear();
    elasticOnly.fatigueStrengthExponent.clear();
    elasticOnly.fatigueDuctilityCoefficient.clear();
    elasticOnly.fatigueDuctilityExponent.clear();
    CHECK(OpenModel::recordFromForm(elasticOnly, record).empty());
    CHECK(!record.fatigue.any());

    OpenModel::MaterialForm bad = steelFormMmTS("steel_d");
    bad.young = "abc";
    CHECK(OpenModel::recordFromForm(bad, record).find("Young modulus") != std::string::npos);

    bad = steelFormMmTS("steel_d");
    bad.fatigueStrengthExponent = "minus point nine";
    CHECK(OpenModel::recordFromForm(bad, record).find("fatigue strength exponent b") != std::string::npos);

    bad = steelFormMmTS("steel_d");
    bad.unit = "furlong";
    CHECK(OpenModel::recordFromForm(bad, record) == "Unknown unit system.");

    bad = steelFormMmTS("steel_d");
    bad.classKey = "unobtainium";
    CHECK(!OpenModel::recordFromForm(bad, record).empty());

    // Physical rules come from validateMaterialRecord: yield above ultimate is refused.
    bad = steelFormMmTS("steel_d");
    bad.yieldStrength = "600";
    CHECK(OpenModel::recordFromForm(bad, record).find("yield strength cannot exceed") != std::string::npos);

    bad = steelFormMmTS("steel_d");
    bad.cyclicStrainHardeningExponent = "1.5";
    CHECK(!OpenModel::recordFromForm(bad, record).empty());
}

void testListJson() {
    const auto path = tempFile("openmodel_material_service_list.db");
    OpenModel::MaterialStore store = openStore(path);
    CHECK(store.isOpen());
    std::string error;
    CHECK(OpenModel::saveMaterialForm(store, steelFormMmTS("steel_e")).ok);

    const std::string json = OpenModel::materialListJson(store, OpenModel::UnitSystem::MmTonneS, path.string(), "");
    CHECK(json.find("\"units\":\"mm-t-s\"") != std::string::npos);
    CHECK(json.find("\"id\":\"steel_s355\"") != std::string::npos);
    CHECK(json.find("\"builtIn\":true") != std::string::npos);
    CHECK(json.find("\"id\":\"steel_e\"") != std::string::npos);
    CHECK(json.find("\"E\":210000") != std::string::npos);            // shown in MPa
    CHECK(json.find("\"yieldStrength\":355") != std::string::npos);   // shown in MPa
    CHECK(json.find("\"fatigueStrengthExponent\":-0.09") != std::string::npos);

    // A built-in material without fatigue data lists null for the fatigue fields.
    CHECK(json.find("\"yieldStrength\":null") != std::string::npos);

    // The same store in SI shows the values in Pa.
    const std::string si = OpenModel::materialListJson(store, OpenModel::UnitSystem::SI, path.string(), "");
    CHECK(si.find("\"units\":\"si\"") != std::string::npos);
    CHECK(si.find("\"yieldStrength\":355000000") != std::string::npos);

    std::filesystem::remove(path);
}

void testJsonEscaping() {
    const auto path = tempFile("openmodel_material_service_escape.db");
    OpenModel::MaterialStore store = openStore(path);
    OpenModel::MaterialForm f = steelFormMmTS("steel_f");
    f.name = "Grade \"A\" \\ special";
    CHECK(OpenModel::saveMaterialForm(store, f).ok);
    const std::string json = OpenModel::materialListJson(store, OpenModel::UnitSystem::SI, path.string(), "line1\nline2");
    CHECK(json.find("Grade \\\"A\\\" \\\\ special") != std::string::npos);
    CHECK(json.find("line1\\nline2") != std::string::npos);
    std::filesystem::remove(path);
}

void testSaveAndDeleteRules() {
    const auto path = tempFile("openmodel_material_service_rules.db");
    OpenModel::MaterialStore store = openStore(path);

    // New material, then the same id again updates it.
    CHECK(OpenModel::saveMaterialForm(store, steelFormMmTS("steel_g")).ok);
    OpenModel::MaterialForm changed = steelFormMmTS("steel_g");
    changed.name = "Renamed";
    const auto updated = OpenModel::saveMaterialForm(store, changed);
    CHECK(updated.ok);
    CHECK(store.find("steel_g") && store.find("steel_g")->elastic.name == "Renamed");

    // Built-in ids cannot be overwritten through the form.
    const auto overwrite = OpenModel::saveMaterialForm(store, steelFormMmTS("steel_s355"));
    CHECK(!overwrite.ok);
    CHECK(store.find("steel_s355") && store.find("steel_s355")->builtIn);

    // Delete: custom yes, built-in no.
    CHECK(!OpenModel::deleteMaterialById(store, "steel_s355").ok);
    CHECK(OpenModel::deleteMaterialById(store, "steel_g").ok);
    CHECK(!store.find("steel_g").has_value());
    CHECK(!OpenModel::deleteMaterialById(store, "steel_g").ok);

    // A closed store refuses everything with a message, never a crash.
    OpenModel::MaterialStore closed;
    CHECK(!OpenModel::saveMaterialForm(closed, steelFormMmTS("steel_h")).ok);
    CHECK(!OpenModel::deleteMaterialById(closed, "steel_h").ok);
    CHECK(!closed.isOpen());
    std::filesystem::remove(path);
}

void testLegacyImport() {
    const auto tsv = tempFile("openmodel_material_service_legacy.tsv");
    std::filesystem::remove(std::filesystem::path(tsv.string() + ".old"));
    {
        std::ofstream out(tsv);
        out << "# id\tname\tclass\tE_Pa\tnu\trho_kg_m3\tnote\n";
        out << "legacy_1\tLegacy one\tsteel\t200000000000\t0.3\t7800\tfrom TSV\n";
    }

    // A store that existed before this run is not refilled from the old file.
    const auto existing = tempFile("openmodel_material_service_existing.db");
    {
        OpenModel::MaterialStore store = openStore(existing);
        CHECK(OpenModel::importLegacyLibraryOnce(store, tsv.string(), false).empty());
        CHECK(!store.find("legacy_1").has_value());
    }

    // A newly created store gets the legacy entries once.
    const auto fresh = tempFile("openmodel_material_service_fresh.db");
    {
        OpenModel::MaterialStore store = openStore(fresh);
        CHECK(OpenModel::importLegacyLibraryOnce(store, tsv.string(), true).empty());
        CHECK(store.find("legacy_1").has_value());
    }
    // The imported file is renamed to .tsv.old and is no longer where it was.
    const auto renamed = std::filesystem::path(tsv.string() + ".old");
    CHECK(!std::filesystem::exists(tsv));
    CHECK(std::filesystem::exists(renamed));

    // A missing legacy file is not an error.
    const auto missing = tempFile("openmodel_material_service_missing.tsv");
    {
        OpenModel::MaterialStore store = openStore(tempFile("openmodel_material_service_nofile.db"));
        CHECK(OpenModel::importLegacyLibraryOnce(store, missing.string(), true).empty());
    }

    std::filesystem::remove(renamed);
    std::filesystem::remove(existing);
    std::filesystem::remove(fresh);
}

} // namespace

int main() {
    testFormConversion();
    testOptionalFieldsAndErrors();
    testListJson();
    testJsonEscaping();
    testSaveAndDeleteRules();
    testLegacyImport();
    if (g_failures == 0) {
        std::cout << "material_service_test: all checks passed\n";
        return 0;
    }
    std::cerr << "material_service_test: " << g_failures << " check(s) failed\n";
    return 1;
}
