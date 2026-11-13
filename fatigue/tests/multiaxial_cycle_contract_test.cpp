#include <OpenModel/Fatigue/MultiaxialCycleCounting.hpp>
#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
ResultField field(std::size_t step, double xx, double yy, double xy) {
    ResultField f;
    f.name = "STRESS";
    f.components = {"SXX", "SYY", "SZZ", "SXY", "SXZ", "SYZ"};
    f.step = step;
    f.location = OpenModel::ResultLocation::Element;
    f.ids = {17};
    f.values = {xx, yy, 0.0, xy, 0.0, 0.0};
    f.semantics.quantity = ResultQuantityKind::Stress;
    f.semantics.components = ResultComponentKind::SymmetricTensor;
    f.semantics.frame = ResultCoordinateFrame::Global;
    f.semantics.unit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    f.semantics.unit.symbol = "Pa";
    f.semantics.unit.scaleToSI = 1.0;
    return f;
}
}

int main() {
    FemResults results;
    results.steps = {
        {"t0", 0.0, StepKind::Time, 0},
        {"t1", 1.0, StepKind::Time, 0},
        {"t2", 2.0, StepKind::Time, 0},
        {"t3", 3.0, StepKind::Time, 0}
    };
    results.fields = {
        field(0, 100.0, 0.0, 0.0),
        field(1, 0.0, 100.0, 20.0),
        field(2, -100.0, 0.0, 0.0),
        field(3, 100.0, 0.0, 0.0)
    };

    OpenModelFemResultsSource source(results);
    ResultPosition position;
    position.location = Fatigue::ResultLocation::Element;
    position.entityId = 17;
    const auto built = StressHistoryBuilder::build(source, "STRESS", position);
    assert(built);

    MultiaxialLoadCycle cycle;
    std::string diagnostic;
    assert(MultiaxialCycleContract::makeCycle(
        built.history, {0, 1, 2, 3}, true, 1.0, 1e-10,
        "test-path-method", cycle, diagnostic));
    assert(cycle.closed);
    assert(cycle.pathSampleIndices.size() == 4);
    assert(cycle.closureResidual < 1e-12);
    assert(cycle.method == "test-path-method");

    // An open path may retain distinct endpoint tensors.
    assert(MultiaxialCycleContract::makeCycle(
        built.history, {0, 1, 2}, false, 0.5, 0.0,
        "test-path-method", cycle, diagnostic));
    assert(!cycle.closed);
    assert(cycle.closureResidual > 0.0);

    assert(!MultiaxialCycleContract::makeCycle(
        built.history, {0, 2, 1}, false, 0.5, 1.0,
        "test-path-method", cycle, diagnostic));
    assert(!MultiaxialCycleContract::makeCycle(
        built.history, {0, 1, 9}, false, 0.5, 1.0,
        "test-path-method", cycle, diagnostic));
    assert(!MultiaxialCycleContract::makeCycle(
        built.history, {0, 1, 2}, true, 1.0, 1e-12,
        "test-path-method", cycle, diagnostic));
    assert(!MultiaxialCycleContract::makeCycle(
        built.history, {0, 1}, false, 0.0, 1.0,
        "test-path-method", cycle, diagnostic));

    const auto d = MultiaxialCycleContract::tensorDistance(
        built.history.samples()[0].prepared.tensor,
        built.history.samples()[3].prepared.tensor);
    assert(std::fabs(d) < 1e-12);
    return 0;
}
