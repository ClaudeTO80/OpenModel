/**
 * @file material_store_test.cpp
 * @brief Checks the SQLite material store: seeding, persistence, validation, CHECK constraints,
 * the TSV import and the schema version.
 */
#include <OpenModel/MaterialDatabase.hpp>
#include <OpenModel/MaterialStore.hpp>

#include <sqlite3.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

/** Values of the fatigue block are test values for the checks, not material data. */
OpenModel::MaterialRecord customRecord(const std::string& id) {
    OpenModel::MaterialRecord record;
    record.elastic = {id, "Test steel " + id, OpenModel::MaterialClass::SteelAndIron, 210.0e9, 0.3, 7850.0,
                      "Test entry, not certified data."};
    record.fatigue.yieldStrengthPa = 355.0e6;
    record.fatigue.ultimateStrengthPa = 470.0e6;
    record.fatigue.cyclicStrengthCoefficientPa = 1000.0e6;
    record.fatigue.cyclicStrainHardeningExponent = 0.2;
    record.fatigue.fatigueStrengthCoefficientPa = 900.0e6;
    record.fatigue.fatigueStrengthExponent = -0.09;
    record.fatigue.fatigueDuctilityCoefficient = 0.5;
    record.fatigue.fatigueDuctilityExponent = -0.6;
    return record;
}

std::filesystem::path tempFile(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / name;
    std::filesystem::remove(path);
    std::filesystem::remove(std::filesystem::path(path.string() + "-journal"));
    return path;
}

/** Runs one SQL statement directly on the file, outside the store, and returns the SQLite result code. */
int rawExecute(const std::filesystem::path& path, const std::string& sql) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
        sqlite3_close(db);
        return SQLITE_CANTOPEN;
    }
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    sqlite3_close(db);
    return rc;
}

void testSeedAndPersistence() {
    const auto path = tempFile("openmodel_material_store_seed.db");
    std::string error;
    {
        OpenModel::MaterialStore store;
        CHECK(OpenModel::MaterialStore::open(path.string(), store, error));
        CHECK(store.list().size() == OpenModel::materialDatabase().size());
        CHECK(store.find("steel_s355") != std::nullopt);
        const auto builtIn = store.find("steel_s355");
        CHECK(builtIn && builtIn->builtIn);
    }
    // Reopening must not duplicate the built-in entries.
    {
        OpenModel::MaterialStore store;
        CHECK(OpenModel::MaterialStore::open(path.string(), store, error));
        CHECK(store.list().size() == OpenModel::materialDatabase().size());
    }
    std::filesystem::remove(path);
}

void testAddUpdateRemove() {
    const auto path = tempFile("openmodel_material_store_crud.db");
    std::string error;
    OpenModel::MaterialStore store;
    CHECK(OpenModel::MaterialStore::open(path.string(), store, error));
    const std::size_t builtInCount = OpenModel::materialDatabase().size();

    CHECK(store.add(customRecord("custom_a"), error));
    CHECK(store.list().size() == builtInCount + 1);

    // Values survive a close and reopen, including the optional fatigue data.
    {
        OpenModel::MaterialStore reopened;
        CHECK(OpenModel::MaterialStore::open(path.string(), reopened, error));
        const auto found = reopened.find("custom_a");
        CHECK(found.has_value());
        if (found) {
            CHECK(!found->builtIn);
            CHECK(std::abs(found->elastic.youngModulus - 210.0e9) < 1.0);
            CHECK(found->fatigue.yieldStrengthPa && std::abs(*found->fatigue.yieldStrengthPa - 355.0e6) < 1.0);
            CHECK(found->fatigue.fatigueStrengthExponent && *found->fatigue.fatigueStrengthExponent == -0.09);
        }
    }

    // Duplicate id, including the id of a built-in entry, is refused.
    CHECK(!store.add(customRecord("custom_a"), error));
    CHECK(!store.add(customRecord("steel_s355"), error));

    // A record without optional fatigue data is valid.
    OpenModel::MaterialRecord elasticOnly = customRecord("custom_b");
    elasticOnly.fatigue = {};
    CHECK(store.add(elasticOnly, error));
    CHECK(store.find("custom_b") && !store.find("custom_b")->fatigue.any());

    // Update: values change, the id does not.
    OpenModel::MaterialRecord changed = customRecord("custom_a");
    changed.elastic.name = "Changed name";
    changed.fatigue.fatigueDuctilityExponent = -0.7;
    CHECK(store.update("custom_a", changed, error));
    CHECK(store.find("custom_a")->elastic.name == "Changed name");
    CHECK(!store.update("custom_a", customRecord("custom_z"), error));

    // Built-in entries cannot be changed or removed.
    CHECK(!store.update("steel_s355", customRecord("steel_s355"), error));
    CHECK(!store.remove("steel_s355", error));

    CHECK(store.remove("custom_a", error));
    CHECK(!store.find("custom_a").has_value());
    CHECK(store.list().size() == builtInCount + 1);

    std::filesystem::remove(path);
}

void testValidationRules() {
    OpenModel::MaterialRecord r = customRecord("rules");
    CHECK(OpenModel::validateMaterialRecord(r).empty());

    r = customRecord("rules");
    r.fatigue.yieldStrengthPa = 600.0e6;  // above the ultimate strength of 470 MPa
    CHECK(!OpenModel::validateMaterialRecord(r).empty());

    r = customRecord("rules");
    r.fatigue.cyclicStrainHardeningExponent = 1.5;
    CHECK(!OpenModel::validateMaterialRecord(r).empty());

    r = customRecord("rules");
    r.fatigue.fatigueStrengthExponent = 0.09;  // Basquin exponent must be negative
    CHECK(!OpenModel::validateMaterialRecord(r).empty());

    r = customRecord("rules");
    r.fatigue.fatigueDuctilityCoefficient = NAN;
    CHECK(!OpenModel::validateMaterialRecord(r).empty());

    r = customRecord("rules");
    r.elastic.poisson = 0.5;  // elastic rules still apply
    CHECK(!OpenModel::validateMaterialRecord(r).empty());
}

void testSqlConstraints() {
    const auto path = tempFile("openmodel_material_store_checks.db");
    std::string error;
    {
        OpenModel::MaterialStore store;
        CHECK(OpenModel::MaterialStore::open(path.string(), store, error));
    }
    // The database refuses these rows even when they bypass the C++ checks.
    const std::string insert =
        "INSERT INTO materials (id, name, class_key, builtin, youngs_modulus_pa, poisson, density_kg_m3, note,"
        " fatigue_strength_exponent) VALUES (";
    CHECK(rawExecute(path, insert + "'ok_row', 'n', 'steel', 0, 210e9, 0.3, 7850, '', -0.09);") == SQLITE_OK);
    CHECK(rawExecute(path, insert + "'bad_e', 'n', 'steel', 0, -1, 0.3, 7850, '', NULL);") == SQLITE_CONSTRAINT);
    CHECK(rawExecute(path, insert + "'Bad_Id', 'n', 'steel', 0, 210e9, 0.3, 7850, '', NULL);") == SQLITE_CONSTRAINT);
    CHECK(rawExecute(path, insert + "'bad_nu', 'n', 'steel', 0, 210e9, 0.5, 7850, '', NULL);") == SQLITE_CONSTRAINT);
    CHECK(rawExecute(path, insert + "'bad_class', 'n', 'unobtainium', 0, 210e9, 0.3, 7850, '', NULL);") ==
          SQLITE_CONSTRAINT);
    CHECK(rawExecute(path, insert + "'bad_b', 'n', 'steel', 0, 210e9, 0.3, 7850, '', 0.09);") == SQLITE_CONSTRAINT);
    std::filesystem::remove(path);
}

void testSchemaVersion() {
    const auto path = tempFile("openmodel_material_store_version.db");
    std::string error;
    {
        OpenModel::MaterialStore store;
        CHECK(OpenModel::MaterialStore::open(path.string(), store, error));
    }
    CHECK(rawExecute(path, "PRAGMA user_version = " + std::to_string(OpenModel::MaterialStore::schemaVersion() + 1) +
                               ";") == SQLITE_OK);
    OpenModel::MaterialStore refused;
    CHECK(!OpenModel::MaterialStore::open(path.string(), refused, error));
    CHECK(error.find("newer version") != std::string::npos);
    std::filesystem::remove(path);
}

void testTsvImport() {
    const auto path = tempFile("openmodel_material_store_import.db");
    const auto tsv = tempFile("openmodel_material_library_import.tsv");
    {
        std::ofstream out(tsv);
        out << "# id\tname\tclass\tE_Pa\tnu\trho_kg_m3\tnote\n";
        out << "imported_1\tImported one\tsteel\t200000000000\t0.3\t7800\tfrom TSV\n";
        out << "steel_s355\tClash with a built-in\tsteel\t200000000000\t0.3\t7800\tignored\n";
        out << "broken\tNo numbers\tsteel\tnot-a-number\t0.3\t7800\tbroken line\n";
    }
    std::string error;
    OpenModel::MaterialStore store;
    CHECK(OpenModel::MaterialStore::open(path.string(), store, error));
    const std::size_t before = store.list().size();
    std::vector<std::string> problems;
    CHECK(store.importTsv(tsv.string(), problems, error));
    CHECK(store.list().size() == before + 1);
    CHECK(store.find("imported_1").has_value());
    CHECK(!store.find("imported_1")->fatigue.any());
    CHECK(!problems.empty());

    // A second import of the same file adds nothing and reports the duplicate.
    problems.clear();
    CHECK(store.importTsv(tsv.string(), problems, error));
    CHECK(store.list().size() == before + 1);
    CHECK(!problems.empty());

    std::filesystem::remove(path);
    std::filesystem::remove(tsv);
}

} // namespace

int main() {
    testSeedAndPersistence();
    testAddUpdateRemove();
    testValidationRules();
    testSqlConstraints();
    testSchemaVersion();
    testTsvImport();
    if (g_failures == 0) {
        std::cout << "material_store_test: all checks passed\n";
        return 0;
    }
    std::cerr << "material_store_test: " << g_failures << " check(s) failed\n";
    return 1;
}
