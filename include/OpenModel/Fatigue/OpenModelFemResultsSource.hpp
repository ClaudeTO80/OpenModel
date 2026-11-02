#pragma once

#include <OpenModel/FemResults.hpp>
#include <OpenModel/Fatigue/FatigueInput.hpp>

namespace OpenModel {
namespace Fatigue {

/**
 * Read-only adapter from the current OpenModel::FemResults representation.
 *
 * FemResults must outlive this object and must not be mutated concurrently.
 * The current core model supports nodal and element records. Integration-point
 * requests are rejected until the importer/storage layer preserves individual
 * integration-point records; an element average is never substituted.
 */
class OpenModelFemResultsSource final : public IResultSource {
public:
    explicit OpenModelFemResultsSource(
        const OpenModel::FemResults& results,
        ResultSourceMetadata metadata = ResultSourceMetadata());

    const ResultSourceMetadata& metadata() const override;
    std::size_t stepCount() const override;
    bool getStep(std::size_t index, ResultStep& out) const override;

    bool readSample(const std::string& fieldName,
                    const ResultPosition& position,
                    std::size_t stepIndex,
                    ResultSample& out) const override;

private:
    const OpenModel::FemResults& results_;
    ResultSourceMetadata metadata_;
};

} // namespace Fatigue
} // namespace OpenModel
