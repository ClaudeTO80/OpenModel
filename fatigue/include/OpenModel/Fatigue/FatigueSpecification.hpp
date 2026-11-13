#pragma once

#include <OpenModel/Fatigue/CycleCounting.hpp>
#include <OpenModel/Fatigue/FatigueMaterial.hpp>
#include <OpenModel/Fatigue/FatigueInput.hpp>
#include <OpenModel/Fatigue/Json.hpp>
#include <OpenModel/FemResults.hpp>

#include <array>
#include <string>
#include <vector>

/**
 * @file FatigueSpecification.hpp
 * @brief Reads the JSON input files of the fatigue analysis: material, result position and load spectrum.
 *
 * These readers only interpret text and values. They do not open result files: the caller (the openmodel-fatigue
 * tool, or an application) reads the listed result files and builds the sources. Every error names the JSON
 * member that failed, for example `curves[0].slices[1].points[3].amplitude: expected a number`.
 *
 * Material file (`readMaterial`):
 * @code
 * {
 *   "name": "S355", "materialId": "S355-base", "source": "datasheet", "notes": "optional",
 *   "curves": [
 *     { "kind": "stressLifeSN",
 *       "amplitudeUnit": { "symbol": "MPa", "scaleToSI": 1e6,
 *                          "dimension": { "mass": 1, "length": -1, "time": -2 } },
 *       "slices": [ { "temperatureKelvin": 293.15,
 *                     "points": [ { "amplitude": 300, "cyclesToFailure": 1e4 },
 *                                 { "amplitude": 150, "cyclesToFailure": 1e6 } ] } ] }
 *   ]
 * }
 * @endcode
 * `kind` is `stressLifeSN` or `strainLifeEN`. The unit's `dimension` lists the exponents of the SI base units
 * (absent members are zero); strain is dimensionless, so its unit has `"dimension": {}`. `offsetToSI` is optional
 * (default 0). Points must be listed in the order the curve validation expects; the curve itself checks them.
 *
 * Position (`parsePosition`): `node:ID`, `element:ID` or `ip:ELEMENT:N`, where N is the 1-based solver
 * integration point.
 *
 * Spectrum file (`readSpectrum`):
 * @code
 * {
 *   "temperatureKelvin": 293.15,
 *   "positions": [
 *     { "label": "root", "position": "node:12",
 *       "blocks": [
 *         { "label": "takeoff", "repetitions": 2, "results": ["takeoff.frd"],
 *           "field": "STRESS", "projection": [1, 0, 0] } ] }
 *   ]
 * }
 * @endcode
 * Result file names are kept as written: the caller resolves them against the spectrum file's directory.
 */

namespace OpenModel {
namespace Fatigue {

/** @brief Outcome of a reader: ok, or an error message naming the failing JSON member. */
struct SpecificationResult {
    bool ok = false;
    std::string error;
    explicit operator bool() const noexcept { return ok; }
};

/** @brief One block of a spectrum file, before its result files are read. */
struct SpectrumBlockSpecification {
    std::string label;
    double repetitions = 1.0;
    std::vector<std::string> results;
    std::string fieldName;
    StressProjection projection;
    /** Unit the result values are declared in (`resultUnit` in the file); see declareFieldUnit(). */
    bool hasResultUnit = false;
    PhysicalUnit resultUnit;
};

/** @brief One analysed position of a spectrum file and its ordered blocks. */
struct SpectrumPositionSpecification {
    std::string label;
    ResultPosition position;
    std::vector<SpectrumBlockSpecification> blocks;
};

/** @brief A whole spectrum file. */
struct SpectrumSpecification {
    double temperatureKelvin = 293.15;
    std::vector<SpectrumPositionSpecification> positions;
};

/** @brief Reads a material from a parsed JSON document (see the file comment). */
SpecificationResult readMaterial(const Json::Value& root, FatigueMaterial& material);

/** @brief Reads `node:ID`, `element:ID` or `ip:ELEMENT:N`. */
SpecificationResult parsePosition(const std::string& text, ResultPosition& position);

/** @brief Reads a spectrum from a parsed JSON document (see the file comment). */
SpecificationResult readSpectrum(const Json::Value& root, SpectrumSpecification& spectrum);

/**
 * @brief Reads a unit declaration `SYMBOL:SCALE` (for example `MPa:1e6`, scale to SI) from the command line.
 *
 * CalculiX result files do not declare a unit system, so the scale of their fields is unknown and the fatigue
 * analysis refuses them until the unit is declared. The dimension is kept from the results.
 */
SpecificationResult parseUnitDeclaration(const std::string& text, PhysicalUnit& unit);

/**
 * @brief Declares the unit of every field called `fieldName` (all steps) in `results`.
 *
 * Only the symbol, scale and offset change; the dimension stays the one the reader found. No conversion is made:
 * the material curve must be declared in exactly this unit. Returns false with `error` when no such field exists.
 */
bool declareFieldUnit(FemResults& results, const std::string& fieldName, const PhysicalUnit& unit, std::string& error);

/** @brief Reads `x,y,z` as a unit direction; rejects a zero or non-finite vector. */
SpecificationResult parseProjection(const std::string& text, StressProjection& projection);

} // namespace Fatigue
} // namespace OpenModel
