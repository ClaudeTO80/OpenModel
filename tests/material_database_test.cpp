/**
 * @file material_database_test.cpp
 * @brief Checks the built-in elastic material database: validity of the entries, unit conversion
 *        and the round trip through the analysis script parser.
 */
#include <OpenModel/AnalysisSetup.hpp>
#include <OpenModel/MaterialDatabase.hpp>

#include <cmath>
#include <iostream>
#include <set>

namespace {

int g_failures = 0;
#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " << #cond << "\n"; \
            ++g_failures;                                                                  \
        }                                                                                  \
    } while (0)

bool close(double a, double b) {
    return std::fabs(a - b) <= 1e-9 * std::fmax(std::fabs(a), std::fabs(b));
}

/** Every entry is physically plausible and self-describing. */
void testEntries() {
    const auto& db = OpenModel::materialDatabase();
    CHECK(db.size() >= 25);
    std::set<std::string> ids;
    for (const auto& m : db) {
        CHECK(!m.id.empty() && !m.name.empty() && !m.note.empty());
        CHECK(m.id.find(' ') == std::string::npos);
        CHECK(ids.insert(m.id).second);                 // unique
        CHECK(m.youngModulus > 0.0);
        CHECK(m.poisson >= 0.0 && m.poisson < 0.5);
        CHECK(m.density > 0.0);
    }
    // Each family is represented.
    std::set<int> classes;
    for (const auto& m : db) classes.insert(static_cast<int>(m.klass));
    CHECK(classes.size() == 8);
}

void testLookup() {
    CHECK(OpenModel::findMaterial("steel_s355") != nullptr);
    CHECK(OpenModel::findMaterial("steel_s355")->klass == OpenModel::MaterialClass::SteelAndIron);
    CHECK(OpenModel::findMaterial("unobtainium") == nullptr);
    CHECK(OpenModel::findMaterial("") == nullptr);
    CHECK(std::string(OpenModel::materialClassName(OpenModel::MaterialClass::Concrete)) == "Concrete");
}

void testUnitParsing() {
    OpenModel::UnitSystem u = OpenModel::UnitSystem::SI;
    CHECK(OpenModel::parseUnitSystem("mm-t-s", u) && u == OpenModel::UnitSystem::MmTonneS);
    CHECK(OpenModel::parseUnitSystem("si", u) && u == OpenModel::UnitSystem::SI);
    CHECK(!OpenModel::parseUnitSystem("imperial", u));
    CHECK(!OpenModel::parseUnitSystem("", u));
}

/** Steel in mm-t-s must give the values of the example scripts: E = 210000 MPa, rho = 7.85e-9 t/mm^3. */
void testConversion() {
    const auto* steel = OpenModel::findMaterial("steel_s355");
    CHECK(steel != nullptr);
    CHECK(close(OpenModel::youngModulusIn(*steel, OpenModel::UnitSystem::SI), 210.0e9));
    CHECK(close(OpenModel::youngModulusIn(*steel, OpenModel::UnitSystem::MmTonneS), 210000.0));
    CHECK(close(OpenModel::densityIn(*steel, OpenModel::UnitSystem::SI), 7850.0));
    CHECK(close(OpenModel::densityIn(*steel, OpenModel::UnitSystem::MmTonneS), 7.85e-9));
}

/** The generated line, parsed back by the solver, gives the same values in both unit systems. */
void testScriptRoundTrip() {
    for (const auto& m : OpenModel::materialDatabase()) {
        for (auto units : {OpenModel::UnitSystem::SI, OpenModel::UnitSystem::MmTonneS}) {
            const std::string line = OpenModel::materialScriptLine(m, units);
            const auto parsed = OpenModel::parseAnalysisScript(line + "\n");
            if (!parsed.success) {
                std::cerr << "  parse failed for: " << line << "\n";
                CHECK(parsed.success);
                continue;
            }
            const auto& mat = parsed.model.material;
            CHECK(mat.name == m.id);                       // the line carries name=<id>
            CHECK(close(mat.youngModulus, OpenModel::youngModulusIn(m, units)));
            CHECK(close(mat.density, OpenModel::densityIn(m, units)));
            CHECK(close(mat.poisson, m.poisson));
        }
    }
}

} // namespace

int main() {
    testEntries();
    testLookup();
    testUnitParsing();
    testConversion();
    testScriptRoundTrip();
    if (g_failures) { std::cerr << g_failures << " check(s) failed\n"; return 1; }
    std::cout << "material_database_test: all passed\n";
    return 0;
}
