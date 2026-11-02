#include <OpenModel/Fatigue/CycleCounting.hpp>
#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
ResultField field(std::size_t step,double xx) {
    ResultField f;
    f.name="STRESS"; f.components={"SXX","SYY","SZZ","SXY","SXZ","SYZ"};
    f.step=step; f.location=OpenModel::ResultLocation::Element; f.ids={7};
    f.values={xx,0,0,0,0,0};
    f.semantics.quantity=ResultQuantityKind::Stress;
    f.semantics.components=ResultComponentKind::SymmetricTensor;
    f.semantics.frame=ResultCoordinateFrame::Global;
    f.semantics.unit.dimension={true,-1,1,-2,0,0,0,0};
    f.semantics.unit.symbol="Pa"; f.semantics.unit.scaleToSI=1.0;
    return f;
}
}

int main() {
    FemResults results;
    const double values[]={0,3,-2,4,-1,0};
    for(std::size_t i=0;i<6;++i) {
        results.steps.push_back({"t",static_cast<double>(i),StepKind::Time,0});
        results.fields.push_back(field(i,values[i]));
    }
    OpenModelFemResultsSource source(results);
    ResultPosition position;
    position.location=Fatigue::ResultLocation::Element;
    position.entityId=7;
    const auto history=StressHistoryBuilder::build(source,"STRESS",position);
    assert(history);

    StressProjection x;
    const auto counted=CycleCounting::rainflow(history.history,x);
    assert(counted);
    assert(!counted.cycles.empty());
    double weighted=0.0;
    for(const auto& cycle:counted.cycles) {
        assert(cycle.range>=0.0);
        assert(cycle.count==0.5 || cycle.count==1.0);
        weighted+=cycle.count;
    }
    assert(weighted>0.0);

    StressProjection invalid;
    invalid.direction={{2,0,0}};
    assert(!CycleCounting::rainflow(history.history,invalid));

    // A constant history contains no reversals and therefore no cycles.
    FemResults constant;
    for(std::size_t i=0;i<3;++i) {
        constant.steps.push_back({"t",static_cast<double>(i),StepKind::Time,0});
        constant.fields.push_back(field(i,5.0));
    }
    OpenModelFemResultsSource constantSource(constant);
    const auto constantHistory=StressHistoryBuilder::build(constantSource,"STRESS",position);
    assert(constantHistory);
    const auto noCycles=CycleCounting::rainflow(constantHistory.history,x);
    assert(noCycles && noCycles.cycles.empty());
    return 0;
}
