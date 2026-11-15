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
#include <cstddef>
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

/** @brief Key of a class in the library file ("aluminium", "steel", ...). */
const char* materialClassKey(MaterialClass klass);

/** @brief Parses a class key. Returns false for an unknown key. */
bool parseMaterialClass(std::string_view key, MaterialClass& out);

/** @brief Converts a Young modulus given in @p units to Pa. */
double youngModulusFromUnits(double value, UnitSystem units);

/** @brief Converts a density given in @p units to kg/m^3. */
double densityFromUnits(double value, UnitSystem units);

/**
 * @brief Checks an entry: id (lower case letters, digits, '_', '-', '.', at most 64 characters),
 * name and note without tabs or line breaks, E > 0, 0 <= nu < 0.5 and rho > 0, all finite.
 * @return Empty when the entry is valid, otherwise the reason.
 */
std::string validateMaterial(const ElasticMaterial& material);

/**
 * @brief The built-in database plus the entries added by the user.
 *
 * Built-in entries are read-only. Custom entries have unique ids that do not clash with the built-in
 * ones. They are kept in a tab-separated file: a header line starting with '#', then one entry per line
 * with the fields id, name, class key, E [Pa], nu, rho [kg/m^3] and note.
 */
class MaterialLibrary {
public:
    /** @brief Library with the built-in entries only. */
    MaterialLibrary();

    /** @brief All entries: built-in first, then custom, in insertion order. */
    const std::vector<ElasticMaterial>& entries() const { return m_entries; }

    /** @brief Entry with the given id, or nullptr. */
    const ElasticMaterial* find(std::string_view id) const;

    /** @brief True when the id belongs to a built-in entry. */
    bool isBuiltIn(std::string_view id) const;

    /** @brief Number of custom entries. */
    std::size_t customCount() const { return m_entries.size() - m_builtInCount; }

    /** @brief Adds a custom entry. Fails when the entry is invalid or the id is already used. */
    bool add(const ElasticMaterial& material, std::string& error);

    /** @brief Replaces a custom entry; the id must not change. Built-in entries cannot be replaced. */
    bool update(std::string_view id, const ElasticMaterial& material, std::string& error);

    /** @brief Removes a custom entry. Built-in entries cannot be removed. */
    bool remove(std::string_view id, std::string& error);

    /**
     * @brief Adds the custom entries of a file. A missing file is not an error (no custom entries).
     * Invalid lines are skipped and described in @p problems, one per line.
     */
    bool load(const std::string& path, std::vector<std::string>& problems, std::string& error);

    /** @brief Writes the custom entries to @p path, through a temporary file (the old file survives a failure). */
    bool save(const std::string& path, std::string& error) const;

private:
    std::vector<ElasticMaterial> m_entries;
    std::size_t m_builtInCount = 0;
};

} // namespace OpenModel
