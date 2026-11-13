#pragma once

#include <OpenModel/Fatigue/StressHistory.hpp>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

/** Explicit scalar projection used for uniaxial cycle counting. */
struct StressProjection {
    // Unit direction expressed in the history's target frame.
    std::array<double, 3> direction{{1.0, 0.0, 0.0}};
};

/** A scalar turning point retained with its original history sample index. */
struct CycleTurningPoint {
    double time = 0.0;
    double value = 0.0;
    std::size_t historySampleIndex = 0;
};

/** One Rainflow cycle. Half cycles have count=0.5, full cycles count=1.0. */
struct LoadCycle {
    CycleTurningPoint start;
    CycleTurningPoint end;
    double range = 0.0;
    double mean = 0.0;
    double count = 0.0;
};

/** Result of scalar cycle counting. */
struct CycleCountingResult {
    bool success = false;
    std::vector<LoadCycle> cycles;
    std::string diagnostic;
    explicit operator bool() const noexcept { return success; }
};

/**
 * Scalar Rainflow counting. A tensor history is reduced only by an explicit
 * user-supplied direction (n^T sigma n or n^T epsilon n). This is a uniaxial
 * projection, not a multiaxial fatigue criterion.
 */
class CycleCounting final {
public:
    static CycleCountingResult rainflow(
        const StressHistory& history,
        const StressProjection& projection);
};

} // namespace Fatigue
} // namespace OpenModel
