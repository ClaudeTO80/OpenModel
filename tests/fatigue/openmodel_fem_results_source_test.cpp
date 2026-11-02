#include <OpenModel/Fatigue/OpenModelFemResultsSource.hpp>

#include <cassert>
#include <cmath>
#include <limits>
#include <sstream>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    FemResults results;
    results.format = "Test solver";
    results.steps.push_back({"load 1", 0.25, StepKind::Time, 0});
    results.steps.push_back({"mode 1", 12.5, StepKind::Frequency, 1});

    ResultField stress;
    stress.name = "STRESS";
    stress.components = {"SXX", "SYY", "SZZ", "SXY", "SXZ", "SYZ"};
    stress.step = 0;
    stress.location = OpenModel::ResultLocation::Element;
    stress.ids = {101, 9001};
    stress.values = {10, 20, 30, 4, 5, 6, 100, 200, 300, 40, 50, 60};
    results.fields.push_back(stress);

    ResultField displacement;
    displacement.name = "DISP";
    displacement.components = {"U1", "U2", "U3"};
    displacement.step = 0;
    displacement.location = OpenModel::ResultLocation::Node;
    displacement.ids = {77};
    displacement.values = {1.0, 2.0, 3.0};
    displacement.semantics.quantity = ResultQuantityKind::Displacement;
    displacement.semantics.components = ResultComponentKind::Vector;
    displacement.semantics.frame = ResultCoordinateFrame::Local;
    displacement.semantics.unit.dimension = {true, 1, 0, 0, 0, 0, 0, 0};
    ResultCoordinateTransform localFrame;
    localFrame.available = true;
    localFrame.name = "element-local-1";
    localFrame.localToGlobal = {{0,-1,0, 1,0,0, 0,0,1}};
    displacement.coordinateTransforms.push_back(localFrame);
    results.fields.push_back(displacement);

    OpenModelFemResultsSource source(results);
    assert(source.metadata().solverName == "Test solver");
    assert(source.metadata().resultFormat == "Test solver");
    assert(source.stepCount() == 2);

    Fatigue::ResultStep step;
    assert(source.getStep(0, step));
    assert(step.parameterKind == StepParameterKind::Time && step.parameter == 0.25);
    assert(source.getStep(1, step));
    assert(step.parameterKind == StepParameterKind::Frequency && step.mode == 1);
    assert(!source.getStep(2, step));

    ResultPosition element;
    element.location = Fatigue::ResultLocation::Element;
    element.entityId = 9001;
    ResultSample sample;
    assert(source.readSample("STRESS", element, 0, sample));
    assert(sample.components.size() == 6);
    assert(sample.componentLabels[4] == "SXZ");
    assert(sample.components[0] == 100.0 && sample.components[5] == 60.0);
    assert(sample.frame == ReferenceFrame::Unknown);
    assert(sample.shearConvention == ShearStrainConvention::Unknown);

    ResultPosition node;
    node.location = Fatigue::ResultLocation::Node;
    node.entityId = 77;
    assert(source.readSample("DISP", node, 0, sample));
    assert(sample.components[2] == 3.0);
    assert(sample.quantity == ResultQuantityKind::Displacement);
    assert(sample.frame == ResultCoordinateFrame::Local);
    assert(sample.coordinateTransform.available);
    assert(sample.coordinateTransform.name == "element-local-1");
    assert(sample.coordinateTransform.localToGlobal[1] == -1.0);

    // Missing records and unsupported IP data must not fall back to element means.
    element.entityId = 123456;
    assert(!source.readSample("STRESS", element, 0, sample));
    element.location = Fatigue::ResultLocation::IntegrationPoint;
    element.entityId = 9001;
    element.integrationPoint = 1;
    assert(!source.readSample("STRESS", element, 0, sample));

    // CalculiX DAT integration-point records are retained independently of
    // the per-element mean generated for viewer compatibility.
    const std::string dat =
        "stresses (elem, integ.pnt., sxx, syy, szz, sxy, sxz, syz) for set EALL and time 1.0\n"
        "\n"
        "9001 1 10 20 30 4 5 6\n"
        "9001 2 30 40 50 6 7 8\n"
        "\n";
    std::istringstream datInput(dat);
    FemResults datResults;
    const ResultImportResult datImport = CalculixDatImporter{}.parse(datInput, datResults);
    assert(datImport);
    assert(datResults.integrationPointFields.size() == 1);
    assert(datResults.integrationPointFields[0].recordCount() == 2);
    assert(datResults.integrationPointFields[0].elementIds[0] == 9001);
    assert(datResults.integrationPointFields[0].integrationPointIds[1] == 2);
    assert(datResults.integrationPointFields[0].semantics.quantity == ResultQuantityKind::Stress);
    assert(datResults.integrationPointFields[0].semantics.components == ResultComponentKind::SymmetricTensor);
    assert(datResults.integrationPointFields[0].semantics.unit.dimension.known);
    assert(datResults.integrationPointFields[0].semantics.unit.dimension.mass == 1);
    assert(datResults.integrationPointFields[0].semantics.unit.scaleToSI == 0.0);
    assert(datResults.integrationPointFields[0].semantics.frame == ResultCoordinateFrame::Global);
    assert(datResults.fields[0].semantics.averaged);
    OpenModelFemResultsSource datSource(datResults);
    ResultPosition ip;
    ip.location = Fatigue::ResultLocation::IntegrationPoint;
    ip.entityId = 9001;
    ip.integrationPoint = 2;
    assert(datSource.readSample("STRESS", ip, 0, sample));
    assert(sample.components[0] == 30.0 && sample.components[5] == 8.0);
    assert(sample.quantity == ResultQuantityKind::Stress);
    assert(sample.componentKind == ResultComponentKind::SymmetricTensor);
    assert(sample.unit.dimension.length == -1 && sample.unit.dimension.mass == 1);
    assert(sample.frame == ResultCoordinateFrame::Global);
    ip.integrationPoint = 3;
    assert(!datSource.readSample("STRESS", ip, 0, sample));

    // Non-finite values and duplicate IDs are rejected.
    results.fields[0].values[6] = (std::numeric_limits<double>::quiet_NaN)();
    OpenModelFemResultsSource nonFinite(results);
    element.location = Fatigue::ResultLocation::Element;
    element.integrationPoint = 0;
    assert(!nonFinite.readSample("STRESS", element, 0, sample));

    results.fields[0].values[6] = 100.0;
    results.fields[0].ids.push_back(9001);
    results.fields[0].values.insert(results.fields[0].values.end(), {1, 2, 3, 4, 5, 6});
    OpenModelFemResultsSource duplicate(results);
    assert(!duplicate.readSample("STRESS", element, 0, sample));

    return 0;
}
