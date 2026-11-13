#pragma once

#include <OpenModel/Fatigue/FatigueInput.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

enum class FatigueCurveKind { StressLifeSN, StrainLifeEN };
enum class FatigueCurveStatus {
    Success,
    EmptyCurve,
    InvalidTemperature,
    DuplicateTemperature,
    InvalidUnit,
    IncompatibleUnit,
    InvalidPoint,
    NonIncreasingAmplitude,
    NonIncreasingLife,
    TemperatureOutOfRange,
    AmplitudeOutOfRange,
    InvalidMaterial
};

struct FatigueCurvePoint {
    double amplitude = 0.0; // Stress or strain amplitude, not range.
    double cyclesToFailure = 0.0;
};

/** One curve at one absolute temperature (kelvin). */
struct FatigueCurveSlice {
    double temperatureKelvin = 293.15;
    std::vector<FatigueCurvePoint> points;
};

/** Explicit policy: this first implementation never extrapolates curves. */
struct FatigueCurveEvaluation {
    FatigueCurveStatus status = FatigueCurveStatus::EmptyCurve;
    double cyclesToFailure = 0.0;
    double log10CyclesToFailure = 0.0;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatigueCurveStatus::Success; }
};

/**
 * Tabulated S-N or strain-life curve family.
 *
 * Interpolation is log-log in amplitude/life and linear in temperature.
 * Temperature interpolation is allowed only within the tabulated temperature
 * range and within the intersection of the amplitude ranges of its bracketing
 * slices. No extrapolation, unit conversion, mean-stress correction or damage
 * accumulation is performed here.
 */
class FatigueCurve final {
public:
    FatigueCurveKind kind() const noexcept { return kind_; }
    const PhysicalUnit& amplitudeUnit() const noexcept { return amplitudeUnit_; }
    const std::vector<FatigueCurveSlice>& slices() const noexcept { return slices_; }

    FatigueCurveEvaluation evaluate(double amplitude, double temperatureKelvin) const;

    static FatigueCurveStatus validate(
        FatigueCurveKind kind,
        const PhysicalUnit& amplitudeUnit,
        const std::vector<FatigueCurveSlice>& slices,
        std::string& diagnostic);

    static bool create(
        FatigueCurveKind kind,
        const PhysicalUnit& amplitudeUnit,
        std::vector<FatigueCurveSlice> slices,
        FatigueCurve& output,
        std::string& diagnostic);

private:
    FatigueCurveKind kind_ = FatigueCurveKind::StressLifeSN;
    PhysicalUnit amplitudeUnit_;
    std::vector<FatigueCurveSlice> slices_;
};

struct FatigueMaterial {
    std::string name;
    std::string materialId;
    std::string source;
    std::string notes;
    std::vector<FatigueCurve> curves;

    const FatigueCurve* findCurve(FatigueCurveKind kind) const noexcept;
    FatigueCurveStatus validate(std::string& diagnostic) const;
};

} // namespace Fatigue
} // namespace OpenModel
