#include <OpenModel/Fatigue/WangBrownRelativeStrain.hpp>
#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <cassert>
#include <cmath>
#include <limits>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
ResultField strainField(std::size_t step, double ex, double ey, double gammaXY) {
    ResultField field;
    field.name = "STRAIN";
    field.components = {"EXX", "EYY", "EZZ", "GXY", "GXZ", "GYZ"};
    field.step = step;
    field.location = OpenModel::ResultLocation::Element;
    field.ids = {9};
    field.values = {ex, ey, 0.0, gammaXY, 0.0, 0.0};
    field.semantics.quantity = ResultQuantityKind::Strain;
    field.semantics.components = ResultComponentKind::SymmetricTensor;
    field.semantics.shearConvention = ResultShearConvention::Engineering;
    field.semantics.frame = ResultCoordinateFrame::Global;
    field.semantics.unit.dimension = {true, 0, 0, 0, 0, 0, 0, 0};
    field.semantics.unit.symbol = "1";
    field.semantics.unit.scaleToSI = 1.0;
    return field;
}
}

int main() {
    Tensor3 a;
    Tensor3 b;
    b.values[0] = 0.01;
    b.values[4] = -0.005;
    b.values[1] = 0.002; // canonical tensor shear, i.e. gamma_xy = 0.004
    const double nu = 0.3;
    const double relative = WangBrownRelativeStrain::between(a, b, nu);
    const double expected = std::sqrt(
        (0.01 - (-0.005)) * (0.01 - (-0.005)) +
        (0.01 - 0.0) * (0.01 - 0.0) +
        ((-0.005) - 0.0) * ((-0.005) - 0.0) +
        6.0 * 0.002 * 0.002) / std::sqrt(2.0 * (1.0 + nu));
    assert(std::fabs(relative - expected) < 1e-14);
    assert(std::fabs(relative - WangBrownRelativeStrain::between(b, a, nu)) < 1e-14);
    assert(WangBrownRelativeStrain::magnitude(b, nu) == relative);
    assert(std::isnan(WangBrownRelativeStrain::between(a, b, 0.5)));
    assert(std::isnan(WangBrownRelativeStrain::between(a, b,
        std::numeric_limits<double>::quiet_NaN())));

    FemResults results;
    results.steps = {
        {"t0", 0.0, StepKind::Time, 0},
        {"t1", 1.0, StepKind::Time, 0},
        {"t2", 2.0, StepKind::Time, 0},
        {"t3", 3.0, StepKind::Time, 0},
        {"t4", 4.0, StepKind::Time, 0},
        {"t5", 5.0, StepKind::Time, 0}
    };
    results.fields = {
        strainField(0, 0.0, 0.0, 0.0),
        strainField(1, 0.01, 0.0, 0.0),
        strainField(2, 0.0, 0.0, 0.0),
        strainField(3, 0.0, 0.01, 0.0),
        strainField(4, 0.0, 0.0, 0.0),
        strainField(5, 0.0, -0.01, 0.0)
    };
    OpenModelFemResultsSource source(results);
    ResultPosition position;
    position.location = Fatigue::ResultLocation::Element;
    position.entityId = 9;
    const auto history = StressHistoryBuilder::build(source, "STRAIN", position);
    assert(history);
    const auto turns = WangBrownRelativeStrain::componentTurningPoints(history.history);
    assert((turns == std::vector<std::size_t>{1, 3, 5}) == false);
    assert(turns.size() == 3);
    assert(turns[0] == 1);
    assert(turns[1] == 3);
    assert(turns[2] == 4);
    return 0;
}
