#include <OpenModel/Fatigue/FatigueSamplePreparer.hpp>
#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {
bool near(double a, double b) { return std::fabs(a-b) < 1e-10*(1.0+std::fabs(a)+std::fabs(b)); }
}

int main() {
    FemResults results;
    results.steps.push_back({"load 1", 1.0, StepKind::Time, 0});

    ResultField stress;
    stress.name = "STRESS";
    stress.components = {"SXX","SYY","SZZ","SXY","SXZ","SYZ"};
    stress.step = 0;
    stress.location = OpenModel::ResultLocation::Element;
    stress.ids = {42};
    stress.values = {100,50,0,0,0,0};
    stress.semantics.quantity = ResultQuantityKind::Stress;
    stress.semantics.components = ResultComponentKind::SymmetricTensor;
    stress.semantics.frame = ResultCoordinateFrame::Global;
    stress.semantics.unit.dimension = {true,-1,1,-2,0,0,0,0};
    results.fields.push_back(stress);

    ResultField strain = stress;
    strain.name = "STRAIN";
    strain.components = {"E11","E22","E33","E12","E13","E23"};
    strain.values = {0.1,0.05,0.0,0.04,0.0,0.0};
    strain.semantics.quantity = ResultQuantityKind::Strain;
    strain.semantics.unit.dimension = {true,0,0,0,0,0,0,0};
    strain.semantics.shear = ResultShearConvention::Engineering;
    results.fields.push_back(strain);

    ResultField averaged = stress;
    averaged.name = "STRESS_AVG";
    averaged.semantics.averaged = true;
    results.fields.push_back(averaged);

    OpenModelFemResultsSource source(results);
    ResultPosition position;
    position.location = Fatigue::ResultLocation::Element;
    position.entityId = 42;

    auto prepared = FatigueSamplePreparer::prepareTensor(source,"STRESS",position,0);
    assert(prepared);
    assert(prepared.vonMisesAvailable && near(prepared.vonMisesStress,std::sqrt(7500.0)));
    assert(prepared.principalValuesAvailable);
    assert(near(prepared.principalValues[0],100));
    assert(near(prepared.principalValues[1],50));
    assert(near(prepared.principalValues[2],0));
    assert(prepared.raw.fieldName == "STRESS");

    auto preparedStrain = FatigueSamplePreparer::prepareTensor(source,"STRAIN",position,0);
    assert(preparedStrain);
    assert(!preparedStrain.vonMisesAvailable);
    assert(near(preparedStrain.tensor(0,1),0.02));

    auto rejectedAverage = FatigueSamplePreparer::prepareTensor(source,"STRESS_AVG",position,0);
    assert(rejectedAverage.status == FatiguePreparationStatus::AveragedSampleRejected);
    FatiguePreparationOptions allowAverage;
    allowAverage.allowAveraged = true;
    assert(FatigueSamplePreparer::prepareTensor(source,"STRESS_AVG",position,0,
        ReferenceFrame::Global,allowAverage));

    // A known but incompatible physical dimension is never silently accepted.
    results.fields[0].semantics.unit.dimension.length = 0;
    OpenModelFemResultsSource badDimension(results);
    assert(FatigueSamplePreparer::prepareTensor(badDimension,"STRESS",position,0).status ==
           FatiguePreparationStatus::IncompatiblePhysicalDimension);

    // Missing source records are distinct from malformed tensor samples.
    position.entityId = 999;
    assert(FatigueSamplePreparer::prepareTensor(source,"STRESS",position,0).status ==
           FatiguePreparationStatus::SourceReadFailed);
    return 0;
}
