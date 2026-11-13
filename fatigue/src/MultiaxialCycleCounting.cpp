#include <OpenModel/Fatigue/MultiaxialCycleCounting.hpp>

#include <cmath>

namespace OpenModel {
namespace Fatigue {

double MultiaxialCycleContract::tensorDistance(const Tensor3& a, const Tensor3& b) noexcept {
    double sum = 0.0;
    for (std::size_t i = 0; i < 9; ++i) {
        const double d = a.values[i] - b.values[i];
        sum += d * d;
    }
    return std::sqrt(sum);
}

bool MultiaxialCycleContract::makeCycle(
    const StressHistory& history,
    const std::vector<std::size_t>& indices,
    bool closed,
    double count,
    double closureTolerance,
    const std::string& method,
    MultiaxialLoadCycle& cycle,
    std::string& diagnostic) {
    if (indices.size() < 2) {
        diagnostic = "A multiaxial path must reference at least two history samples.";
        return false;
    }
    if (!std::isfinite(count) || count <= 0.0) {
        diagnostic = "Cycle count weight must be finite and positive.";
        return false;
    }
    if (!std::isfinite(closureTolerance) || closureTolerance < 0.0) {
        diagnostic = "Closure tolerance must be finite and non-negative.";
        return false;
    }
    if (method.empty()) {
        diagnostic = "A non-empty counting method identifier is required.";
        return false;
    }
    if (history.empty()) {
        diagnostic = "Cannot create a cycle from an empty history.";
        return false;
    }

    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] >= history.size()) {
            diagnostic = "Cycle path contains an out-of-range history sample index.";
            return false;
        }
        if (!history.samples()[indices[i]].prepared ||
            !std::isfinite(history.samples()[indices[i]].time)) {
            diagnostic = "Cycle path references an invalid tensor sample.";
            return false;
        }
        if (i > 0 && indices[i] <= indices[i - 1]) {
            diagnostic = "Cycle path indices must be strictly increasing in history order.";
            return false;
        }
    }

    const double residual = tensorDistance(
        history.samples()[indices.front()].prepared.tensor,
        history.samples()[indices.back()].prepared.tensor);
    if (!std::isfinite(residual)) {
        diagnostic = "Cycle closure residual is not finite.";
        return false;
    }
    if (closed && residual > closureTolerance) {
        diagnostic = "Closed cycle endpoints exceed the declared tensor closure tolerance.";
        return false;
    }

    MultiaxialLoadCycle candidate;
    candidate.pathSampleIndices = indices;
    candidate.closed = closed;
    candidate.count = count;
    candidate.closureResidual = residual;
    candidate.method = method;
    cycle = candidate;
    diagnostic.clear();
    return true;
}

} // namespace Fatigue
} // namespace OpenModel
