/**
 * @file MaterialStore.hpp
 * @brief SQLite-backed store of materials: elastic data (FEM), cyclic plastic data and fatigue constants.
 *
 * One table holds every material, built-in ones included, so that the FEM script and the fatigue analysis
 * refer to the same identifiers. The database enforces the physical limits of the values (CHECK constraints)
 * and the same identifier rules as validateMaterial(); the checks that need the meaning of a value (for example
 * that the exponent of a Basquin law is negative) are done here, before the write.
 *
 * Every value is stored in SI units (Pa, kg/m^3, dimensionless): unit conversion is done by the callers with
 * the functions of MaterialDatabase.hpp, so the database never holds a value in an unknown unit.
 *
 * Built-in entries are read-only. Custom entries can be added, changed and removed.
 *
 * The SQLite engine is embedded (3dParty/sqlite) and is not visible in this header.
 */
#pragma once

#include <OpenModel/MaterialDatabase.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace OpenModel {

/**
 * @brief Cyclic plastic and fatigue constants of a material, in SI (Pa and dimensionless).
 *
 * Each field is optional: a material without fatigue data is valid and can still be used by the FEM script.
 * The names follow the parameters of the fatigue module (Basquin, Coffin-Manson, cyclic Ramberg-Osgood).
 */
struct MaterialFatigueData {
    std::optional<double> yieldStrengthPa;                 ///< Yield strength Sy [Pa].
    std::optional<double> ultimateStrengthPa;              ///< Ultimate tensile strength Su [Pa].
    std::optional<double> cyclicStrengthCoefficientPa;     ///< Cyclic strength coefficient K' [Pa], 0 < K'.
    std::optional<double> cyclicStrainHardeningExponent;   ///< Cyclic strain hardening exponent n', 0 < n' <= 1.
    std::optional<double> fatigueStrengthCoefficientPa;    ///< Fatigue strength coefficient sigma_f' [Pa], > 0.
    std::optional<double> fatigueStrengthExponent;         ///< Fatigue strength exponent b, b < 0.
    std::optional<double> fatigueDuctilityCoefficient;     ///< Fatigue ductility coefficient epsilon_f' [-], > 0.
    std::optional<double> fatigueDuctilityExponent;        ///< Fatigue ductility exponent c, c < 0.

    /** @brief True when at least one field is set. */
    bool any() const noexcept;
};

/** @brief One material: elastic data (as in MaterialDatabase) plus optional fatigue data. */
struct MaterialRecord {
    ElasticMaterial elastic;   ///< Id, name, class, E, nu, rho (SI) and note.
    bool builtIn = false;      ///< True for the entries shipped with the program (read-only).
    MaterialFatigueData fatigue;
};

/**
 * @brief Physical checks of a record that the database cannot express with CHECK constraints alone.
 * @return Empty when the record is valid, otherwise the reason.
 */
std::string validateMaterialRecord(const MaterialRecord& record);

/** @brief SQLite database of materials. Move-only; the file stays open while the object lives. */
class MaterialStore {
public:
    /** @brief A store that is not open: every operation fails until open() succeeds on an object. */
    MaterialStore();

    /**
     * @brief Opens (or creates) the database at @p path, applies the schema and seeds the built-in entries.
     * @return False with @p error set when the file cannot be opened or its schema is not supported.
     */
    static bool open(const std::string& path, MaterialStore& out, std::string& error);

    MaterialStore(MaterialStore&&) noexcept;
    MaterialStore& operator=(MaterialStore&&) noexcept;
    MaterialStore(const MaterialStore&) = delete;
    MaterialStore& operator=(const MaterialStore&) = delete;
    ~MaterialStore();

    /** @brief Record with the given id, or nothing. */
    std::optional<MaterialRecord> find(std::string_view id) const;

    /** @brief All records, built-in first, then custom, in insertion order. */
    std::vector<MaterialRecord> list() const;

    /** @brief Adds a custom record. Fails when it is invalid or the id is already used. */
    bool add(const MaterialRecord& record, std::string& error);

    /** @brief Replaces a custom record; the id must not change. Built-in records cannot be replaced. */
    bool update(std::string_view id, const MaterialRecord& record, std::string& error);

    /** @brief Removes a custom record. Built-in records cannot be removed. */
    bool remove(std::string_view id, std::string& error);

    /**
     * @brief Imports the custom entries of a tab-separated library file (the format of MaterialLibrary::save).
     * Entries already in the store are not overwritten: they are reported in @p problems and skipped.
     * Invalid lines are reported in @p problems too. @return False only on a database or file error.
     */
    bool importTsv(const std::string& path, std::vector<std::string>& problems, std::string& error);

    /** @brief True when the database is open. */
    bool isOpen() const noexcept;

    /** @brief Path of the database file; empty when the store is not open. */
    const std::string& path() const noexcept;

    /** @brief Schema version applied to the file (see the migration code in MaterialStore.cpp). */
    static int schemaVersion() noexcept;

private:
    class Impl;
    explicit MaterialStore(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace OpenModel
