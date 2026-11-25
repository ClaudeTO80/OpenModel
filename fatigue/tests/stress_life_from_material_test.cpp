/**
 * @file stress_life_from_material_test.cpp
 * @brief Tests of the material-to-fatigue bridge: Basquin S-N curves, unit handling, errors and position lists.
 */

#include <OpenModel/Fatigue/StressLifeFromMaterial.hpp>

#include <cassert>
#include <cmath>
#include <cstdio>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {

/** Steel-like record with Basquin constants sigma_f' = 900 MPa and b = -0.09 (SI in the database). */
MaterialRecord basquinRecord() {
    MaterialRecord record{};
    record.elastic.id = "steel-test";
    record.elastic.name = "Steel test";
    record.elastic.youngModulus = 2.1e11;
    record.elastic.poisson = 0.3;
    record.elastic.density = 7850.0;
    record.fatigue.fatigueStrengthCoefficientPa = 900.0e6;
    record.fatigue.fatigueStrengthExponent = -0.09;
    return record;
}

/** Basquin life N = (2N)/2 with 2N = (a / sigma_f')^(1/b), for an amplitude in the same unit as sigma_f'. */
double basquinCycles(double amplitudeSameUnit, double sigmaF, double b) {
    return 0.5 * std::pow(amplitudeSameUnit / sigmaF, 1.0 / b);
}

} // namespace

int main() {
    const double kT = 293.15;

    // 1. The table reproduces the Basquin law exactly inside its range (log-log interpolation of a power law).
    PhysicalUnit mpa = pressureUnit("MPa", 1.0e6);
    assert(mpa.dimension.known);
    assert(mpa.dimension.mass == 1 && mpa.dimension.length == -1 && mpa.dimension.time == -2);

    FatigueMaterial material;
    std::string error;
    assert(stressLifeCurveFromBasquin(basquinRecord(), kT, mpa, material, error));
    assert(error.empty());
    assert(material.materialId == "steel-test");
    assert(material.name == "Steel test");
    const FatigueCurve* curve = material.findCurve(FatigueCurveKind::StressLifeSN);
    assert(curve != nullptr);

    const double sigmaMpa = 900.0;
    for (double amplitude : {50.0, 100.0, 300.0, 650.0, 899.0}) {
        const auto result = curve->evaluate(amplitude, kT);
        assert(result);
        const double expected = basquinCycles(amplitude, sigmaMpa, -0.09);
        assert(std::fabs(result.cyclesToFailure - expected) <= 1e-9 * expected);
    }

    // 2. The unit changes the numbers, not the physics: the same life at the same physical amplitude in Pa.
    PhysicalUnit pa = pressureUnit("Pa", 1.0);
    FatigueMaterial inPa;
    assert(stressLifeCurveFromBasquin(basquinRecord(), kT, pa, inPa, error));
    const auto atPa = inPa.findCurve(FatigueCurveKind::StressLifeSN)->evaluate(300.0e6, kT);
    const auto atMpa = curve->evaluate(300.0, kT);
    assert(atPa && atMpa);
    assert(std::fabs(atPa.cyclesToFailure - atMpa.cyclesToFailure) <= 1e-9 * atMpa.cyclesToFailure);

    // 3. Outside the tabulated range the module refuses instead of extrapolating: above sigma_f' there is no
    //    Basquin life (2N < 1), and below the last tabulated amplitude the table ends.
    assert(curve->evaluate(950.0, kT).status == FatigueCurveStatus::AmplitudeOutOfRange);
    // The smallest tabulated amplitude is sigma_f' (1e15)^b = 900 * 10^-1.35 MPa, about 40 MPa.
    assert(curve->evaluate(10.0, kT).status == FatigueCurveStatus::AmplitudeOutOfRange);

    // 4. The database holds one temperature: other temperatures are outside the single slice.
    assert(curve->evaluate(300.0, 500.0).status == FatigueCurveStatus::TemperatureOutOfRange);

    // 5. Missing or unusable constants are reported, never replaced by defaults.
    MaterialRecord noFatigue = basquinRecord();
    noFatigue.fatigue = MaterialFatigueData{};
    FatigueMaterial rejected;
    error.clear();
    assert(!stressLifeCurveFromBasquin(noFatigue, kT, mpa, rejected, error));
    assert(error.find("sigma_f'") != std::string::npos);

    MaterialRecord positiveExponent = basquinRecord();
    positiveExponent.fatigue.fatigueStrengthExponent = 0.1;
    assert(!stressLifeCurveFromBasquin(positiveExponent, kT, mpa, rejected, error));
    assert(error.find("negative") != std::string::npos);

    MaterialRecord zeroStrength = basquinRecord();
    zeroStrength.fatigue.fatigueStrengthCoefficientPa = 0.0;
    assert(!stressLifeCurveFromBasquin(zeroStrength, kT, mpa, rejected, error));

    assert(!stressLifeCurveFromBasquin(basquinRecord(), 0.0, mpa, rejected, error));
    assert(!stressLifeCurveFromBasquin(basquinRecord(), kT, pressureUnit("bad", 0.0), rejected, error));

    // 6. Positions: each item is read by parsePosition, separators are ';' and line breaks, blanks are ignored.
    std::vector<ResultPosition> positions;
    assert(parsePositionList(" node:1 ; element:2\n\n ip:3:4 ;", positions, error));
    assert(positions.size() == 3);
    assert(positions[0].location == Fatigue::ResultLocation::Node && positions[0].entityId == 1);
    assert(positions[1].location == Fatigue::ResultLocation::Element && positions[1].entityId == 2);
    assert(positions[2].location == Fatigue::ResultLocation::IntegrationPoint && positions[2].entityId == 3 &&
           positions[2].integrationPoint == 4);

    // The label of a position is accepted back by the parser: the report and the input use the same text.
    for (const auto& p : positions) {
        std::vector<ResultPosition> back;
        assert(parsePositionList(positionLabel(p), back, error));
        assert(back.size() == 1 && back[0].location == p.location && back[0].entityId == p.entityId &&
               back[0].integrationPoint == p.integrationPoint);
    }
    assert(positionLabel(positions[2]) == "ip:3:4");

    assert(!parsePositionList("", positions, error));
    assert(error.find("at least one") != std::string::npos);
    assert(!parsePositionList("node:1; banana:7", positions, error));
    assert(error.find("banana:7") != std::string::npos);

    std::printf("stress_life_from_material_test: all checks passed\n");
    return 0;
}
