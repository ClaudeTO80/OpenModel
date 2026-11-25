/**
 * @file MaterialService.hpp
 * @brief Logic behind the Material tab: form parsing, unit conversion, JSON listing and the store operations.
 *
 * The workbench handlers only read their arguments, call these functions and reply. Everything here is plain
 * C++ with no WebUI dependency, so it is tested directly (tests/material_service_test.cpp).
 *
 * Units: the form is in one unit system ("si" or "mm-t-s"); the store is always SI. Stresses use the same
 * conversion as the Young modulus (MPa to Pa in mm-t-s), densities the same as the density functions.
 */
#pragma once

#include <OpenModel/MaterialStore.hpp>

#include <string>
#include <string_view>

namespace OpenModel {

/**
 * @brief The material form as text, exactly as the page sends it. An empty text means "not given".
 * Stress fields (yield, ultimate, K', sigma_f') are in the unit system of @ref unit; the exponents and
 * epsilon_f' are dimensionless and are not converted.
 */
struct MaterialForm {
    std::string unit;   ///< "si" or "mm-t-s".
    std::string id;
    std::string name;
    std::string classKey;
    std::string young;     ///< E, in the unit system of @ref unit.
    std::string poisson;   ///< nu.
    std::string density;   ///< rho, in the unit system of @ref unit.
    std::string note;

    std::string yieldStrength;
    std::string ultimateStrength;
    std::string cyclicStrengthCoefficient;
    std::string cyclicStrainHardeningExponent;
    std::string fatigueStrengthCoefficient;
    std::string fatigueStrengthExponent;
    std::string fatigueDuctilityCoefficient;
    std::string fatigueDuctilityExponent;
};

/** @brief Outcome of a store operation: a message for the page, and whether it succeeded. */
struct MaterialServiceReply {
    bool ok = false;
    std::string message;
};

/** @brief Strict number: the whole text must be a finite number. */
bool parseFiniteNumber(const std::string& text, double& out);

/** @brief Unit system from its text ("si" or "mm-t-s"); false for anything else. */
bool parseFormUnit(const std::string& text, UnitSystem& out);

/**
 * @brief Converts a form to a record in SI and checks it.
 * @return Empty when the record is valid, otherwise the message for the user (the field that failed is named).
 */
std::string recordFromForm(const MaterialForm& form, MaterialRecord& record);

/**
 * @brief JSON for the Material tab: classes, the materials (values in @p units) and the store location.
 * Absent fatigue values are null. @p note reports problems found while opening the store, if any.
 */
std::string materialListJson(const MaterialStore& store, UnitSystem units, const std::string& storePath,
                             const std::string& note);

/** @brief Adds the form as a new material, or updates the custom material with the same id. */
MaterialServiceReply saveMaterialForm(MaterialStore& store, const MaterialForm& form);

/** @brief Removes a custom material. Built-in materials are refused with a message. */
MaterialServiceReply deleteMaterialById(MaterialStore& store, const std::string& id);

/**
 * @brief First-run import of the old tab-separated library. Done only when @p storeWasCreated is true, that is
 * when the database file did not exist before this run: a material the user deleted later must not come back.
 * After a successful import the file is renamed to "<path>.old" (materials.tsv becomes materials.tsv.old), so it
 * is never imported again. Returns the problems found (duplicates, invalid lines, rename failure) as text, empty
 * when none or when nothing was imported.
 */
std::string importLegacyLibraryOnce(MaterialStore& store, const std::string& legacyTsvPath, bool storeWasCreated);

} // namespace OpenModel
