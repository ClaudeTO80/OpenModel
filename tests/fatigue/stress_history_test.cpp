#include <OpenModel/Fatigue/StressHistory.hpp>
#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
ResultField stressField(std::size_t step, double xx) {
    ResultField field;
    field.name = "STRESS";
    field.components = {"SXX", "SYY", "SZZ", "SXY", "SXZ", "SYZ"};
    field.step = step;
    field.location = OpenModel::ResultLocation::Element;
    field.ids = {42};
    field.values = {xx, 50.0, 0.0, 0.0, 0.0, 0.0};
    field.semantics.quantity = ResultQuantityKind::Stress;
    field.semantics.components = ResultComponentKind::SymmetricTensor;
    field.semantics.frame = ResultCoordinateFrame::Global;
    field.semantics.unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    field.semantics.unit.symbol = "Pa";
    field.semantics.unit.scaleToSI = 1.0;
    return field;
}
}

int main() {
    FemResults results;
    results.steps = {
        {"t=0", 0.0, StepKind::Time, 0},
        {"t=0.5", 0.5, StepKind::Time, 0},
        {"t=1", 1.0, StepKind::Time, 0}
    };
    results.fields.push_back(stressField(0, 100.0));
    results.fields.push_back(stressField(1, 120.0));
    results.fields.push_back(stressField(2, 80.0));

    OpenModelFemResultsSource source(results);
    ResultPosition position;
    position.location = Fatigue::ResultLocation::Element;
    position.entityId = 42;

    auto built = StressHistoryBuilder::build(source, "STRESS", position);
    assert(built);
    assert(built.history.size() == 3);
    assert(built.history.quantity() == QuantityKind::Stress);
    assert(built.history.frame() == ReferenceFrame::Global);
    assert(built.history.samples()[0].time == 0.0);
    assert(built.history.samples()[1].time == 0.5);
    assert(built.history.samples()[2].time == 1.0);
    assert(built.history.samples()[1].sourceStepIndex == 1);
    assert(built.history.samples()[1].prepared.tensor(0, 0) == 120.0);
    assert(built.history.samples()[1].prepared.raw.position.entityId == 42);

    // Step indices are not accepted as a substitute for physical time.
    results.steps[1].kind = StepKind::LoadStep;
    OpenModelFemResultsSource loadFactorSource(results);
    assert(StressHistoryBuilder::build(loadFactorSource, "STRESS", position).status ==
           StressHistoryStatus::NonTimeStep);

    results.steps[1].kind = StepKind::Time;
    results.steps[1].value = 0.0;
    OpenModelFemResultsSource duplicateTimeSource(results);
    assert(StressHistoryBuilder::build(duplicateTimeSource, "STRESS", position).status ==
           StressHistoryStatus::NonIncreasingTime);

    results.steps[1].value = 0.5;
    results.fields[2].semantics.unit.symbol = "MPa";
    results.fields[2].semantics.unit.scaleToSI = 1.0e6;
    OpenModelFemResultsSource mixedUnitsSource(results);
    assert(StressHistoryBuilder::build(mixedUnitsSource, "STRESS", position).status ==
           StressHistoryStatus::InconsistentUnits);

    return 0;
}
