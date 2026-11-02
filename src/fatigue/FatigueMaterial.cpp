#include <OpenModel/Fatigue/FatigueMaterial.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace OpenModel {
namespace Fatigue {
namespace {

bool validAmplitudeUnit(FatigueCurveKind kind, const PhysicalUnit& unit) {
    if (!unit.dimension.known || unit.symbol.empty() ||
        !std::isfinite(unit.scaleToSI) || unit.scaleToSI <= 0.0 ||
        !std::isfinite(unit.offsetToSI) || unit.offsetToSI != 0.0)
        return false;
    const PhysicalDimension expected = kind == FatigueCurveKind::StressLifeSN
        ? PhysicalDimension{true, -1, 1, -2, 0, 0, 0, 0}
        : PhysicalDimension{true, 0, 0, 0, 0, 0, 0, 0};
    return unit.dimension.known == expected.known &&
        unit.dimension.length == expected.length &&
        unit.dimension.mass == expected.mass &&
        unit.dimension.time == expected.time &&
        unit.dimension.current == expected.current &&
        unit.dimension.temperature == expected.temperature &&
        unit.dimension.amount == expected.amount &&
        unit.dimension.luminousIntensity == expected.luminousIntensity;
}

bool sameUnit(const PhysicalUnit& a, const PhysicalUnit& b) {
    return a.dimension.known == b.dimension.known &&
        a.dimension.length == b.dimension.length &&
        a.dimension.mass == b.dimension.mass &&
        a.dimension.time == b.dimension.time &&
        a.dimension.current == b.dimension.current &&
        a.dimension.temperature == b.dimension.temperature &&
        a.dimension.amount == b.dimension.amount &&
        a.dimension.luminousIntensity == b.dimension.luminousIntensity &&
        a.symbol == b.symbol && a.scaleToSI == b.scaleToSI &&
        a.offsetToSI == b.offsetToSI;
}

double interpolateLogLog(const std::vector<FatigueCurvePoint>& points, double amplitude) {
    const auto it = std::lower_bound(points.begin(), points.end(), amplitude,
        [](const FatigueCurvePoint& p, double x) { return p.amplitude < x; });
    if (it == points.begin()) return std::log10(it->cyclesToFailure);
    if (it == points.end()) return std::log10(points.back().cyclesToFailure);
    if (it->amplitude == amplitude) return std::log10(it->cyclesToFailure);
    const auto& b = *it;
    const auto& a = *(it - 1);
    const double x = std::log10(amplitude);
    const double xa = std::log10(a.amplitude);
    const double xb = std::log10(b.amplitude);
    const double ya = std::log10(a.cyclesToFailure);
    const double yb = std::log10(b.cyclesToFailure);
    return ya + (yb - ya) * ((x - xa) / (xb - xa));
}

} // namespace

FatigueCurveStatus FatigueCurve::validate(
    FatigueCurveKind kind, const PhysicalUnit& unit,
    const std::vector<FatigueCurveSlice>& slices, std::string& diagnostic) {
    if (slices.empty()) {
        diagnostic = "At least one temperature slice is required.";
        return FatigueCurveStatus::EmptyCurve;
    }
    if (!validAmplitudeUnit(kind, unit)) {
        diagnostic = kind == FatigueCurveKind::StressLifeSN
            ? "S-N amplitude unit must have stress dimension and a known positive SI scale."
            : "Strain-life amplitude unit must be dimensionless with a known positive scale.";
        return FatigueCurveStatus::InvalidUnit;
    }

    double previousTemperature = -std::numeric_limits<double>::infinity();
    for (const auto& slice : slices) {
        if (!std::isfinite(slice.temperatureKelvin) || slice.temperatureKelvin <= 0.0) {
            diagnostic = "Curve temperature must be finite, positive and expressed in kelvin.";
            return FatigueCurveStatus::InvalidTemperature;
        }
        if (slice.temperatureKelvin <= previousTemperature) {
            diagnostic = "Temperature slices must be strictly increasing and unique.";
            return FatigueCurveStatus::DuplicateTemperature;
        }
        previousTemperature = slice.temperatureKelvin;
        if (slice.points.size() < 2) {
            diagnostic = "Each temperature slice requires at least two points.";
            return FatigueCurveStatus::InvalidPoint;
        }
        double previousAmplitude = 0.0;
        double previousLife = std::numeric_limits<double>::infinity();
        for (const auto& point : slice.points) {
            if (!std::isfinite(point.amplitude) || point.amplitude <= 0.0 ||
                !std::isfinite(point.cyclesToFailure) || point.cyclesToFailure <= 0.0) {
                diagnostic = "Curve amplitudes and cycles to failure must be finite and positive.";
                return FatigueCurveStatus::InvalidPoint;
            }
            if (point.amplitude <= previousAmplitude) {
                diagnostic = "Amplitudes must be strictly increasing in every slice.";
                return FatigueCurveStatus::NonIncreasingAmplitude;
            }
            if (point.cyclesToFailure >= previousLife) {
                diagnostic = "Cycles to failure must strictly decrease as amplitude increases.";
                return FatigueCurveStatus::NonIncreasingLife;
            }
            previousAmplitude = point.amplitude;
            previousLife = point.cyclesToFailure;
        }
    }
    diagnostic.clear();
    return FatigueCurveStatus::Success;
}

bool FatigueCurve::create(
    FatigueCurveKind kind, const PhysicalUnit& unit,
    std::vector<FatigueCurveSlice> slices, FatigueCurve& output,
    std::string& diagnostic) {
    const auto status = validate(kind, unit, slices, diagnostic);
    if (status != FatigueCurveStatus::Success) return false;
    FatigueCurve candidate;
    candidate.kind_ = kind;
    candidate.amplitudeUnit_ = unit;
    candidate.slices_ = std::move(slices);
    output = std::move(candidate);
    return true;
}

FatigueCurveEvaluation FatigueCurve::evaluate(double amplitude, double temperatureKelvin) const {
    FatigueCurveEvaluation result;
    if (!std::isfinite(amplitude) || amplitude <= 0.0 ||
        !std::isfinite(temperatureKelvin) || temperatureKelvin <= 0.0) {
        result.status = FatigueCurveStatus::InvalidPoint;
        result.diagnostic = "Amplitude and absolute temperature must be finite and positive.";
        return result;
    }
    if (slices_.empty()) {
        result.status = FatigueCurveStatus::EmptyCurve;
        result.diagnostic = "Curve has no temperature slices.";
        return result;
    }
    if (temperatureKelvin < slices_.front().temperatureKelvin ||
        temperatureKelvin > slices_.back().temperatureKelvin) {
        result.status = FatigueCurveStatus::TemperatureOutOfRange;
        result.diagnostic = "Temperature extrapolation is disabled.";
        return result;
    }

    const auto upper = std::lower_bound(slices_.begin(), slices_.end(), temperatureKelvin,
        [](const FatigueCurveSlice& s, double t) { return s.temperatureKelvin < t; });
    const FatigueCurveSlice* low = nullptr;
    const FatigueCurveSlice* high = nullptr;
    if (upper != slices_.end() && upper->temperatureKelvin == temperatureKelvin) {
        low = high = &*upper;
    } else {
        high = &*upper;
        low = &*(upper - 1);
    }

    const double minAmplitude = std::max(low->points.front().amplitude, high->points.front().amplitude);
    const double maxAmplitude = std::min(low->points.back().amplitude, high->points.back().amplitude);
    if (amplitude < minAmplitude || amplitude > maxAmplitude) {
        result.status = FatigueCurveStatus::AmplitudeOutOfRange;
        result.diagnostic = "Amplitude is outside the common interpolation range; extrapolation is disabled.";
        return result;
    }

    const double logLifeLow = interpolateLogLog(low->points, amplitude);
    const double logLifeHigh = interpolateLogLog(high->points, amplitude);
    double logLife = logLifeLow;
    if (low != high) {
        const double fraction = (temperatureKelvin - low->temperatureKelvin) /
            (high->temperatureKelvin - low->temperatureKelvin);
        logLife += fraction * (logLifeHigh - logLifeLow);
    }
    result.log10CyclesToFailure = logLife;
    result.cyclesToFailure = std::pow(10.0, logLife);
    if (!std::isfinite(result.cyclesToFailure) || result.cyclesToFailure <= 0.0) {
        result.status = FatigueCurveStatus::InvalidPoint;
        result.diagnostic = "Interpolated fatigue life is outside the representable numeric range.";
        return result;
    }
    result.status = FatigueCurveStatus::Success;
    return result;
}

const FatigueCurve* FatigueMaterial::findCurve(FatigueCurveKind kind) const noexcept {
    for (const auto& curve : curves)
        if (curve.kind() == kind) return &curve;
    return nullptr;
}

FatigueCurveStatus FatigueMaterial::validate(std::string& diagnostic) const {
    if (name.empty()) {
        diagnostic = "Material name must not be empty.";
        return FatigueCurveStatus::InvalidMaterial;
    }
    if (curves.empty()) {
        diagnostic = "Material must define at least one fatigue curve.";
        return FatigueCurveStatus::InvalidMaterial;
    }
    for (std::size_t i = 0; i < curves.size(); ++i) {
        if (findCurve(curves[i].kind()) != &curves[i]) {
            diagnostic = "A material may define at most one curve family of each kind.";
            return FatigueCurveStatus::InvalidMaterial;
        }
        std::string curveDiagnostic;
        const auto status = FatigueCurve::validate(
            curves[i].kind(), curves[i].amplitudeUnit(), curves[i].slices(), curveDiagnostic);
        if (status != FatigueCurveStatus::Success) {
            diagnostic = curveDiagnostic;
            return status;
        }
    }
    diagnostic.clear();
    return FatigueCurveStatus::Success;
}

} // namespace Fatigue
} // namespace OpenModel
