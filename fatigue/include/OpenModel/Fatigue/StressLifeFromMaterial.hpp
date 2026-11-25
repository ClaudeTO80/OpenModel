/**
 * @file StressLifeFromMaterial.hpp
 * @brief Bridge from the material database to the fatigue module: the S-N curve of a material and the position lists.
 *
 * The material database stores Basquin constants (sigma_f', b) in SI units; the fatigue module works on tabulated
 * curves in the unit of the FEM results. This header turns one into the other without changing the physics: the
 * tabulated points lie exactly on the Basquin law, and the module interpolates log-log, which reproduces a power law
 * exactly. The curve does not extrapolate: amplitudes outside the tabulated range are reported as errors by the
 * module, never clamped.
 */
#pragma once

#include <OpenModel/MaterialStore.hpp>
#include <OpenModel/Fatigue/FatigueMaterial.hpp>
#include <OpenModel/Fatigue/FatigueSpecification.hpp>

#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

/**
 * @brief Stress unit of the given symbol and scale to Pa, with the dimension of a pressure (mass / (length time^2)).
 *
 * The unit of the curve and the unit declared on the result field must be identical (dimension, symbol, scale), so
 * both are built with this function from the same symbol and scale.
 */
PhysicalUnit pressureUnit(const std::string& symbol, double scaleToSI);

/**
 * @brief Tabulated S-N curve of a material at one temperature, built from its Basquin constants.
 *
 * The law is sigma_a = sigma_f' (2N)^b with 2N the reversals to failure. The table covers 2N from 1 to 1e15 in
 * steps of half a decade. Amplitudes are stored in @p amplitudeUnit; the constants are converted from Pa to that
 * unit. The curve has a single temperature slice: the database holds no temperature dependence, so the curve is
 * valid at @p temperatureKelvin only.
 *
 * @param record material from the database; it must carry sigma_f' > 0 and b < 0.
 * @param temperatureKelvin absolute temperature of the analysis (K), positive.
 * @param amplitudeUnit unit of the cycle amplitudes, with a positive scale to Pa.
 * @param out receives the material on success.
 * @param error receives the reason on failure.
 * @return false when the material lacks the constants or a value is not usable.
 */
bool stressLifeCurveFromBasquin(
    const MaterialRecord& record,
    double temperatureKelvin,
    const PhysicalUnit& amplitudeUnit,
    FatigueMaterial& out,
    std::string& error);

/**
 * @brief Canonical text of a position: "node:ID", "element:ID" or "ip:ELEMENT:N".
 * The text is accepted back by parsePosition().
 */
std::string positionLabel(const ResultPosition& position);

/**
 * @brief Reads a list of positions separated by ';' or line breaks; each item is read by parsePosition().
 *
 * Blank items are ignored. The list must hold at least one position; the first invalid item stops the reading and
 * its reason is reported in @p error, which names the item.
 */
bool parsePositionList(const std::string& text, std::vector<ResultPosition>& positions, std::string& error);

} // namespace Fatigue
} // namespace OpenModel
