/**
 * @file MaterialDatabase.hpp
 * @brief Built-in database of linear elastic materials (Young modulus, Poisson ratio, density).
 *
 * The values are stored in SI units. They are typical values for each family, not certified
 * material data: each entry states its basis in @ref ElasticMaterial::note. Check them against
 * the datasheet or the design code before using them in a verification.
 *
 * The database only provides data. The solver script line is produced by materialScriptLine(),
 * which converts the values to the unit system chosen by the user (both systems are consistent).
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace OpenModel {

/** @brief Family of an entry; used to group the entries in the interface. */
enum class MaterialClass {
    Aluminium,
    SteelAndIron,
    OtherMetal,
    Concrete,
    Wood,
    Polymer,
    GlassCeramic,
    Stone
};

/** @brief Unit system of a solver script. Both are internally consistent (force, length, mass). */
enum class UnitSystem {
    SI,        ///< metre, pascal (N/m^2), kilogram per cubic metre.
    MmTonneS   ///< millimetre, megapascal (N/mm^2), tonne per cubic millimetre.
};

/** @brief One entry of the database. Values in SI units. */
struct ElasticMaterial {
    std::string id;        ///< Stable identifier: lower case, no spaces, usable as a script name.
    std::string name;      ///< Display name.
    MaterialClass klass;   ///< Family of the material.
    double youngModulus;   ///< Young modulus [Pa].
    double poisson;        ///< Poisson ratio [-].
    double density;        ///< Density [kg/m^3].
    std::string note;      ///< Basis of the values and limits of use.
};

/** @brief The whole database, in a fixed order (grouped by class). */
const std::vector<ElasticMaterial>& materialDatabase();

/** @brief Entry with the given id, or nullptr when there is none. */
const ElasticMaterial* findMaterial(std::string_view id);

/** @brief Human-readable name of a class. */
const char* materialClassName(MaterialClass klass);

/** @brief Parses "si" or "mm-t-s". Returns false for any other text. */
bool parseUnitSystem(std::string_view text, UnitSystem& out);

/** @brief Young modulus of the entry in the given unit system (Pa or MPa). */
double youngModulusIn(const ElasticMaterial& material, UnitSystem units);

/** @brief Density of the entry in the given unit system (kg/m^3 or t/mm^3). */
double densityIn(const ElasticMaterial& material, UnitSystem units);

/**
 * @brief Script line for the entry, e.g. "material E=210000 nu=0.3 rho=7.85e-09 name=steel_s355".
 * Values are expressed in @p units, so the line can be appended to a script in that system.
 */
std::string materialScriptLine(const ElasticMaterial& material, UnitSystem units);

} // namespace OpenModel
