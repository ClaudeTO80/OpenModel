#pragma once

#include <OpenModel/Fatigue/StressHistory.hpp>

#include <cstddef>
#include <optional>

namespace OpenModel {
namespace Fatigue {

/**
 * Geometric primitives used by the original Wang-Brown multiaxial reversal
 * locator. They operate on a full strain tensor history; no component
 * filtering or critical-plane projection is performed.
 */
class WangBrownRelativeStrain final {
public:
    /**
     * Relative Mises strain between two tensor states, as used by Wang-Brown
     * multiaxial rainflow counting:
     *
     * sqrt((dx-dy)^2 + (dx-dz)^2 + (dy-dz)^2
     *      + 6*(dExy^2+dExz^2+dEyz^2)) / (sqrt(2*(1+nuBar))).
     *
     * Tensor shear components are tensorial (gamma/2), consistent with
     * TensorNormalizer's canonical Tensor3 representation.
     */
    static double between(
        const Tensor3& reference,
        const Tensor3& current,
        double effectivePoissonRatio) noexcept;

    /** Absolute equivalent value, measured relative to the zero tensor. */
    static double magnitude(
        const Tensor3& value,
        double effectivePoissonRatio) noexcept;

    /** Index of the sample with the largest absolute relative-Mises magnitude. */
    static std::optional<std::size_t> maximumMagnitudeIndex(
        const StressHistory& history,
        double effectivePoissonRatio);

    /**
     * Index of the largest relative-Mises distance from one reference sample.
     * The inspected interval is [beginIndex, endIndex); invalid ranges return
     * std::nullopt. Ties are resolved in favour of the earliest sample.
     */
    static std::optional<std::size_t> maximumRelativeIndex(
        const StressHistory& history,
        std::size_t referenceIndex,
        double effectivePoissonRatio,
        std::size_t beginIndex,
        std::size_t endIndex);

    /**
     * Return original sample indices at which at least one of the six
     * independent strain components has a strict local maximum or minimum.
     * The input is not decimated: returned indices always refer to the
     * original StressHistory.
     */
    static std::vector<std::size_t> componentTurningPoints(
        const StressHistory& history);
};

} // namespace Fatigue
} // namespace OpenModel
