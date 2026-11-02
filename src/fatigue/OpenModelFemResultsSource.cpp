#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <cmath>
#include <limits>
#include <utility>

namespace OpenModel {
namespace Fatigue {
namespace {

StepParameterKind toParameterKind(OpenModel::StepKind kind) {
    switch (kind) {
    case OpenModel::StepKind::Time: return StepParameterKind::Time;
    case OpenModel::StepKind::Frequency: return StepParameterKind::Frequency;
    case OpenModel::StepKind::LoadStep: return StepParameterKind::LoadFactor;
    case OpenModel::StepKind::Static: return StepParameterKind::None;
    case OpenModel::StepKind::User: return StepParameterKind::User;
    }
    return StepParameterKind::User;
}

ResultStep convertStep(const OpenModel::ResultStep& source, std::size_t index) {
    ResultStep step;
    step.index = index;
    step.parameter = source.value;
    step.parameterKind = toParameterKind(source.kind);
    step.mode = source.mode;
    step.label = source.label;
    return step;
}

} // namespace

OpenModelFemResultsSource::OpenModelFemResultsSource(
    const OpenModel::FemResults& results,
    ResultSourceMetadata metadata)
    : results_(results), metadata_(std::move(metadata)) {
    if (metadata_.solverName.empty()) metadata_.solverName = results_.format;
    if (metadata_.resultFormat.empty()) metadata_.resultFormat = results_.format;
}

const ResultSourceMetadata& OpenModelFemResultsSource::metadata() const {
    return metadata_;
}

std::size_t OpenModelFemResultsSource::stepCount() const {
    return results_.steps.size();
}

bool OpenModelFemResultsSource::getStep(std::size_t index, ResultStep& out) const {
    if (index >= results_.steps.size()) return false;
    out = convertStep(results_.steps[index], index);
    return true;
}

bool OpenModelFemResultsSource::readSample(
    const std::string& fieldName,
    const ResultPosition& position,
    std::size_t stepIndex,
    ResultSample& out) const {
    if (stepIndex >= results_.steps.size() || position.location == ResultLocation::IntegrationPoint)
        return false;
    if (position.integrationPoint != 0 || position.sectionPoint != 0)
        return false;

    const OpenModel::ResultLocation wantedLocation =
        position.location == ResultLocation::Node
            ? OpenModel::ResultLocation::Node
            : OpenModel::ResultLocation::Element;

    const OpenModel::ResultField* matched = nullptr;
    for (const OpenModel::ResultField& field : results_.fields) {
        if (field.name == fieldName && field.step == stepIndex && field.location == wantedLocation) {
            if (matched != nullptr) return false; // duplicate field key is ambiguous
            matched = &field;
        }
    }
    if (matched == nullptr || matched->componentCount() == 0) return false;

    const std::size_t recordCount = matched->recordCount();
    const std::size_t componentCount = matched->componentCount();
    if (componentCount != 0 &&
        recordCount > (std::numeric_limits<std::size_t>::max)() / componentCount)
        return false;
    if (matched->values.size() != recordCount * componentCount) return false;

    std::size_t record = recordCount;
    for (std::size_t i = 0; i < recordCount; ++i) {
        if (matched->ids[i] == position.entityId) {
            if (record != recordCount) return false; // duplicate external ID
            record = i;
        }
    }
    if (record == recordCount) return false;

    ResultSample candidate;
    candidate.position = position;
    candidate.step = convertStep(results_.steps[stepIndex], stepIndex);
    candidate.fieldName = matched->name;
    candidate.componentLabels = matched->components;
    candidate.components.reserve(componentCount);

    const std::size_t offset = record * componentCount;
    for (std::size_t c = 0; c < componentCount; ++c) {
        const double value = matched->values[offset + c];
        if (!std::isfinite(value)) return false;
        candidate.components.push_back(value);
    }

    // FemResults currently does not store tensor semantics, coordinate frame,
    // units or averaging provenance. Leave these explicitly Unknown/unset.
    out = std::move(candidate);
    return true;
}

} // namespace Fatigue
} // namespace OpenModel
