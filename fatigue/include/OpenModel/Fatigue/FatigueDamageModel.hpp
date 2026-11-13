#pragma once

#include <OpenModel/Fatigue/CycleCounting.hpp>
#include <OpenModel/Fatigue/FatigueMaterial.hpp>
#include <OpenModel/Fatigue/MeanStressCorrection.hpp>

#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

enum class FatigueDamageStatus {
    Success,
    InvalidInput,
    IncompatibleQuantity,
    IncompatibleUnit,
    CurveEvaluationFailed,
    NumericalOverflow,
    UnsupportedModel
};

struct FatigueDamageResult {
    FatigueDamageStatus status = FatigueDamageStatus::InvalidInput;
    double damage = 0.0;
    double blockLife = 0.0; // Number of repetitions of this counted block at D=1.
    std::size_t cyclesEvaluated = 0;
    std::size_t zeroRangeCycles = 0;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatigueDamageStatus::Success; }
};

/** Input to a damage model. Units must match the selected material curve exactly. */
struct FatigueDamageInput {
    const std::vector<LoadCycle>* cycles = nullptr;
    const FatigueCurve* curve = nullptr;
    PhysicalUnit amplitudeUnit;
    double temperatureKelvin = 293.15;
    MeanStressCorrectionKind meanStressCorrection = MeanStressCorrectionKind::None;
    MeanStressCorrectionParameters meanStressParameters;
};

/** Solver-neutral extension point for cumulative fatigue damage models. */
class IFatigueDamageModel {
public:
    virtual ~IFatigueDamageModel() = default;
    virtual std::string name() const = 0;
    virtual FatigueDamageResult evaluate(const FatigueDamageInput& input) const = 0;
};

/**
 * Linear Palmgren-Miner rule for stress-life curves.
 *
 * Each Rainflow range is converted to amplitude (range/2), then contributes
 * count/N(amplitude,T). No mean-stress correction, sequence effect, residual
 * strength, or unit conversion is applied.
 */
class PalmgrenMiner final : public IFatigueDamageModel {
public:
    std::string name() const override { return "Palmgren-Miner"; }
    FatigueDamageResult evaluate(const FatigueDamageInput& input) const override;
};

} // namespace Fatigue
} // namespace OpenModel
