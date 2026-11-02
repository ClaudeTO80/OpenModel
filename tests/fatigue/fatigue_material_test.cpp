#include <OpenModel/Fatigue/FatigueMaterial.hpp>

#include <cassert>
#include <cmath>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    PhysicalUnit stressUnit;
    stressUnit.dimension = {true, -1, 1, -2, 0, 0, 0, 0};
    stressUnit.symbol = "MPa";
    stressUnit.scaleToSI = 1.0e6;

    std::vector<FatigueCurveSlice> slices{
        {293.15, {{100.0, 1.0e6}, {200.0, 1.0e5}, {400.0, 1.0e4}}},
        {393.15, {{100.0, 8.0e5}, {200.0, 8.0e4}, {400.0, 8.0e3}}}
    };
    FatigueCurve curve;
    std::string diagnostic;
    assert(FatigueCurve::create(FatigueCurveKind::StressLifeSN,
        stressUnit, slices, curve, diagnostic));

    auto atPoint = curve.evaluate(200.0, 293.15);
    assert(atPoint);
    assert(std::fabs(atPoint.cyclesToFailure - 1.0e5) < 1e-6);

    auto between = curve.evaluate(200.0, 343.15);
    assert(between);
    assert(std::fabs(between.cyclesToFailure - std::sqrt(8.0e9)) < 1.0);

    assert(curve.evaluate(50.0, 293.15).status == FatigueCurveStatus::AmplitudeOutOfRange);
    assert(curve.evaluate(200.0, 250.0).status == FatigueCurveStatus::TemperatureOutOfRange);
    assert(curve.evaluate(0.0, 293.15).status == FatigueCurveStatus::InvalidPoint);

    auto invalid = slices;
    invalid[0].points[1].cyclesToFailure = 2.0e6;
    assert(FatigueCurve::validate(FatigueCurveKind::StressLifeSN,
        stressUnit, invalid, diagnostic) == FatigueCurveStatus::NonIncreasingLife);

    PhysicalUnit strainUnit;
    strainUnit.dimension = {true, 0, 0, 0, 0, 0, 0, 0};
    strainUnit.symbol = "1";
    strainUnit.scaleToSI = 1.0;
    FatigueCurve strainCurve;
    assert(FatigueCurve::create(FatigueCurveKind::StrainLifeEN, strainUnit,
        {{293.15, {{0.001, 1.0e7}, {0.01, 1.0e5}}}}, strainCurve, diagnostic));

    FatigueMaterial material;
    material.name = "Test steel";
    material.materialId = "TEST-001";
    material.curves = {curve, strainCurve};
    assert(material.validate(diagnostic) == FatigueCurveStatus::Success);
    assert(material.findCurve(FatigueCurveKind::StressLifeSN) != nullptr);
    assert(material.findCurve(FatigueCurveKind::StrainLifeEN) != nullptr);

    FatigueMaterial duplicate;
    duplicate.name = "Invalid";
    duplicate.curves = {curve, curve};
    assert(duplicate.validate(diagnostic) == FatigueCurveStatus::InvalidMaterial);
    return 0;
}
