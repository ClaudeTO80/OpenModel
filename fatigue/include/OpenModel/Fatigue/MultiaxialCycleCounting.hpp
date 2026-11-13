#pragma once

#include <OpenModel/Fatigue/StressHistory.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace OpenModel {
namespace Fatigue {

/**
 * A multiaxial cycle candidate represented by its complete ordered tensor
 * trajectory. Indices refer to StressHistory::samples(), not copied scalars.
 */
struct MultiaxialLoadCycle {
    std::vector<std::size_t> pathSampleIndices;
    bool closed = false;
    double count = 1.0;
    double closureResidual = 0.0; // Frobenius norm in the history's tensor unit.
    std::string method;
};

/** Output of a multiaxial cycle-counting implementation. */
struct MultiaxialCycleCountingResult {
    bool success = false;
    std::vector<MultiaxialLoadCycle> cycles;
    std::string method;
    std::string diagnostic;
    explicit operator bool() const noexcept { return success; }
};

/** Validation utilities shared by multiaxial cycle-counting algorithms. */
class MultiaxialCycleContract final {
public:
    /**
     * Create a cycle candidate from strictly increasing history sample indices.
     * A closed path must have matching first/last tensors within the absolute
     * tolerance expressed in the history's stress/strain unit.
     */
    static bool makeCycle(
        const StressHistory& history,
        const std::vector<std::size_t>& pathSampleIndices,
        bool closed,
        double count,
        double closureTolerance,
        const std::string& method,
        MultiaxialLoadCycle& cycle,
        std::string& diagnostic);

    static double tensorDistance(const Tensor3& a, const Tensor3& b) noexcept;
};

/**
 * Extension point for genuine multiaxial counting methods (critical-plane,
 * path-based, invariant-based, etc.). Implementations must return path
 * references, not only scalar equivalent ranges, so downstream criteria can
 * inspect the original tensor trajectory.
 */
class IMultiaxialCycleCounter {
public:
    virtual ~IMultiaxialCycleCounter() = default;
    virtual MultiaxialCycleCountingResult count(const StressHistory& history) const = 0;
};

} // namespace Fatigue
} // namespace OpenModel
