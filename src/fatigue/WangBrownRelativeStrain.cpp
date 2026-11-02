#include <OpenModel/Fatigue/WangBrownRelativeStrain.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace OpenModel {
namespace Fatigue {
namespace {

bool finiteTensor(const Tensor3& tensor) noexcept {
    for (double value : tensor.values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

} // namespace

double WangBrownRelativeStrain::between(
    const Tensor3& reference,
    const Tensor3& current,
    double effectivePoissonRatio) noexcept {
    if (!finiteTensor(reference) || !finiteTensor(current) ||
        !std::isfinite(effectivePoissonRatio) ||
        effectivePoissonRatio <= -1.0 || effectivePoissonRatio >= 0.5) {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double dx = current.values[0] - reference.values[0];
    const double dy = current.values[4] - reference.values[4];
    const double dz = current.values[8] - reference.values[8];
    // Tensor3 stores tensorial shear strain; engineering gamma is 2*epsilon_xy.
    const double dxy = current.values[1] - reference.values[1];
    const double dxz = current.values[2] - reference.values[2];
    const double dyz = current.values[5] - reference.values[5];

    const double numerator =
        (dx - dy) * (dx - dy) +
        (dx - dz) * (dx - dz) +
        (dy - dz) * (dy - dz) +
        6.0 * (dxy * dxy + dxz * dxz + dyz * dyz);
    const double denominator = std::sqrt(2.0) * (1.0 + effectivePoissonRatio);
    const double value = std::sqrt(std::max(0.0, numerator)) / denominator;
    return std::isfinite(value) ? value : std::numeric_limits<double>::quiet_NaN();
}

double WangBrownRelativeStrain::magnitude(
    const Tensor3& value,
    double effectivePoissonRatio) noexcept {
    const Tensor3 zero{};
    return between(zero, value, effectivePoissonRatio);
}


std::optional<std::size_t> WangBrownRelativeStrain::maximumMagnitudeIndex(
    const StressHistory& history,
    double effectivePoissonRatio) {
    if (history.quantity() != QuantityKind::Strain || history.empty() ||
        !std::isfinite(effectivePoissonRatio) ||
        effectivePoissonRatio <= -1.0 || effectivePoissonRatio >= 0.5) {
        return std::nullopt;
    }

    std::optional<std::size_t> maximumIndex;
    double maximumValue = -1.0;
    const auto& samples = history.samples();
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (!samples[i].prepared) continue;
        const double value = magnitude(samples[i].prepared.tensor, effectivePoissonRatio);
        if (!std::isfinite(value)) continue;
        if (!maximumIndex || value > maximumValue) {
            maximumIndex = i;
            maximumValue = value;
        }
    }
    return maximumIndex;
}

std::optional<std::size_t> WangBrownRelativeStrain::maximumRelativeIndex(
    const StressHistory& history,
    std::size_t referenceIndex,
    double effectivePoissonRatio,
    std::size_t beginIndex,
    std::size_t endIndex) {
    if (history.quantity() != QuantityKind::Strain ||
        referenceIndex >= history.size() ||
        beginIndex >= endIndex || endIndex > history.size() ||
        !history.samples()[referenceIndex].prepared ||
        !std::isfinite(effectivePoissonRatio) ||
        effectivePoissonRatio <= -1.0 || effectivePoissonRatio >= 0.5) {
        return std::nullopt;
    }

    const Tensor3& reference = history.samples()[referenceIndex].prepared.tensor;
    std::optional<std::size_t> maximumIndex;
    double maximumValue = -1.0;
    const auto& samples = history.samples();
    for (std::size_t i = beginIndex; i < endIndex; ++i) {
        if (!samples[i].prepared) continue;
        const double value = between(reference, samples[i].prepared.tensor,
                                     effectivePoissonRatio);
        if (!std::isfinite(value)) continue;
        if (!maximumIndex || value > maximumValue) {
            maximumIndex = i;
            maximumValue = value;
        }
    }
    return maximumIndex;
}

std::vector<std::size_t> WangBrownRelativeStrain::componentTurningPoints(
    const StressHistory& history) {
    std::vector<std::size_t> points;
    if (history.quantity() != QuantityKind::Strain || history.size() < 3) {
        return points;
    }

    const auto& samples = history.samples();
    std::vector<bool> isTurning(samples.size(), false);
    constexpr std::size_t tensorIndices[6] = {0, 4, 8, 1, 2, 5};

    // Process contiguous valid runs independently. A plateau is one extremum,
    // represented by its middle original sample.
    for (std::size_t component = 0; component < 6; ++component) {
        std::size_t runBegin = 0;
        while (runBegin < samples.size()) {
            while (runBegin < samples.size() &&
                   (!samples[runBegin].prepared ||
                    !finiteTensor(samples[runBegin].prepared.tensor))) {
                ++runBegin;
            }
            if (runBegin >= samples.size()) break;

            std::size_t runEnd = runBegin;
            while (runEnd + 1 < samples.size() &&
                   samples[runEnd + 1].prepared &&
                   finiteTensor(samples[runEnd + 1].prepared.tensor)) {
                ++runEnd;
            }

            std::size_t plateauBegin = runBegin;
            while (plateauBegin <= runEnd) {
                std::size_t plateauEnd = plateauBegin;
                const double level =
                    samples[plateauBegin].prepared.tensor.values[tensorIndices[component]];
                while (plateauEnd + 1 <= runEnd &&
                       samples[plateauEnd + 1].prepared.tensor.values[tensorIndices[component]] == level) {
                    ++plateauEnd;
                }

                if (plateauBegin > runBegin && plateauEnd < runEnd) {
                    const double before =
                        samples[plateauBegin - 1].prepared.tensor.values[tensorIndices[component]];
                    const double after =
                        samples[plateauEnd + 1].prepared.tensor.values[tensorIndices[component]];
                    if ((level > before && level > after) ||
                        (level < before && level < after)) {
                        isTurning[plateauBegin + (plateauEnd - plateauBegin) / 2] = true;
                    }
                }
                plateauBegin = plateauEnd + 1;
            }
            runBegin = runEnd + 1;
        }
    }

    for (std::size_t i = 0; i < isTurning.size(); ++i) {
        if (isTurning[i]) points.push_back(i);
    }
    return points;
}

} // namespace Fatigue
} // namespace OpenModel
