/**
 * @file material_library_test.cpp
 * @brief Checks the material library: built-in protection, custom entries, validation and the library file.
 */
#include <OpenModel/MaterialDatabase.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

OpenModel::ElasticMaterial sample(const std::string& id) {
    return {id, "Test alloy " + id, OpenModel::MaterialClass::OtherMetal, 90.0e9, 0.3, 4000.0, "Test entry."};
}

void testValidation() {
    CHECK(OpenModel::validateMaterial(sample("ok_1.2-x")).empty());
    CHECK(!OpenModel::validateMaterial(sample("Bad Id")).empty());     // upper case and space
    CHECK(!OpenModel::validateMaterial(sample("")).empty());
    CHECK(!OpenModel::validateMaterial(sample(std::string(65, 'a'))).empty());
    auto m = sample("x");
    m.youngModulus = 0.0;  CHECK(!OpenModel::validateMaterial(m).empty());
    m = sample("x");  m.poisson = 0.5;  CHECK(!OpenModel::validateMaterial(m).empty());
    m = sample("x");  m.density = NAN;  CHECK(!OpenModel::validateMaterial(m).empty());
    m = sample("x");  m.note = "a\tb";  CHECK(!OpenModel::validateMaterial(m).empty());
    m = sample("x");  m.name = "";      CHECK(!OpenModel::validateMaterial(m).empty());
}

void testClassKeys() {
    OpenModel::MaterialClass k{};
    CHECK(OpenModel::parseMaterialClass("steel", k) && k == OpenModel::MaterialClass::SteelAndIron);
    CHECK(!OpenModel::parseMaterialClass("unknown", k));
    for (int i = 0; i < 8; ++i) {
        const auto klass = static_cast<OpenModel::MaterialClass>(i);
        OpenModel::MaterialClass back{};
        CHECK(OpenModel::parseMaterialClass(OpenModel::materialClassKey(klass), back) && back == klass);
    }
}

void testUnitsToSi() {
    CHECK(std::fabs(OpenModel::youngModulusFromUnits(210000.0, OpenModel::UnitSystem::MmTonneS) - 210.0e9) < 1.0);
    CHECK(std::fabs(OpenModel::densityFromUnits(7.85e-9, OpenModel::UnitSystem::MmTonneS) - 7850.0) < 1e-6);
    CHECK(OpenModel::youngModulusFromUnits(2.1e11, OpenModel::UnitSystem::SI) == 2.1e11);
}

void testLibraryRules() {
    OpenModel::MaterialLibrary lib;
    const std::size_t builtIn = lib.entries().size();
    CHECK(builtIn >= 25 && lib.customCount() == 0);
    CHECK(lib.isBuiltIn("steel_s355") && !lib.isBuiltIn("custom_x"));

    std::string error;
    CHECK(lib.add(sample("custom_a"), error) && error.empty());
    CHECK(lib.customCount() == 1 && lib.find("custom_a") != nullptr);
    CHECK(!lib.add(sample("custom_a"), error) && error.find("already") != std::string::npos);
    CHECK(!lib.add(sample("steel_s355"), error) && error.find("built-in") != std::string::npos);

    // Built-in entries are read-only.
    CHECK(!lib.update("steel_s355", sample("steel_s355"), error));
    CHECK(!lib.remove("steel_s355", error));
    CHECK(lib.find("steel_s355") != nullptr);

    // Update keeps the id; a changed id is refused.
    auto changed = sample("custom_a");
    changed.youngModulus = 70.0e9;
    CHECK(lib.update("custom_a", changed, error));
    CHECK(std::fabs(lib.find("custom_a")->youngModulus - 70.0e9) < 1.0);
    CHECK(!lib.update("custom_a", sample("custom_b"), error));
    CHECK(!lib.update("custom_missing", sample("custom_missing"), error));

    CHECK(lib.remove("custom_a", error) && lib.customCount() == 0);
    CHECK(!lib.remove("custom_a", error));
    CHECK(lib.entries().size() == builtIn);
}

void testFileRoundTrip() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "openmodel_material_library_test";
    fs::remove_all(dir);
    const std::string path = (dir / "nested" / "materials.tsv").string();

    OpenModel::MaterialLibrary lib;
    std::string error;
    CHECK(lib.add(sample("custom_one"), error));
    auto second = sample("custom_two");
    second.youngModulus = 1.0 / 3.0 * 1e9;     // a value that needs full precision
    second.note = "with spaces, and punctuation: ok";
    CHECK(lib.add(second, error));
    CHECK(lib.save(path, error) && error.empty());
    CHECK(!fs::exists(path + ".tmp"));

    OpenModel::MaterialLibrary reloaded;
    std::vector<std::string> problems;
    CHECK(reloaded.load(path, problems, error) && problems.empty());
    CHECK(reloaded.customCount() == 2);
    const auto* back = reloaded.find("custom_two");
    CHECK(back != nullptr && back->youngModulus == second.youngModulus && back->note == second.note);
    CHECK(back && back->klass == second.klass && back->density == second.density);

    // A missing file means no custom entries.
    OpenModel::MaterialLibrary none;
    CHECK(none.load((dir / "absent.tsv").string(), problems, error) && none.customCount() == 0);

    fs::remove_all(dir);
}

void testBadFileLines() {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "openmodel_material_library_bad";
    fs::create_directories(dir);
    const std::string path = (dir / "bad.tsv").string();
    {
        std::ofstream out(path);
        out << "# header\n"
            << "good\tGood\tsteel\t2.1e11\t0.3\t7850\tok\n"
            << "wrongfields\tonly two\n"
            << "badclass\tX\tunobtainium\t1e9\t0.3\t1000\t\n"
            << "badnumber\tX\tsteel\tabc\t0.3\t1000\t\n"
            << "steel_s355\tDup\tsteel\t2.1e11\t0.3\t7850\t\n"   // clashes with a built-in id
            << "badnu\tX\tsteel\t2.1e11\t0.7\t7850\t\n";
    }
    OpenModel::MaterialLibrary lib;
    std::vector<std::string> problems;
    std::string error;
    CHECK(lib.load(path, problems, error));
    CHECK(lib.customCount() == 1 && lib.find("good") != nullptr);
    CHECK(problems.size() == 5);
    fs::remove_all(dir);
}

} // namespace

int main() {
    testValidation();
    testClassKeys();
    testUnitsToSi();
    testLibraryRules();
    testFileRoundTrip();
    testBadFileLines();
    if (g_failures) { std::cerr << g_failures << " check(s) failed\n"; return 1; }
    std::cout << "material_library_test: all passed\n";
    return 0;
}
