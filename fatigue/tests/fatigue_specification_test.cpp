#include <OpenModel/Fatigue/FatigueSpecification.hpp>

#include <cassert>
#include <cmath>
#include <string>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

namespace {

Json::Value json(const std::string& text) {
    const auto parsed = Json::parse(text);
    assert(parsed.ok);
    return parsed.value;
}

/** A valid material with one S-N curve, and the text of its fields that the failure tests change. */
std::string material(const std::string& kind = "stressLifeSN", const std::string& unit = "\"symbol\": \"MPa\", \"scaleToSI\": 1e6",
                      const std::string& points = "{\"amplitude\": 150, \"cyclesToFailure\": 1e6}, {\"amplitude\": 300, \"cyclesToFailure\": 1e4}") {
    return "{\"name\": \"steel\", \"curves\": [{\"kind\": \"" + kind + "\", \"amplitudeUnit\": {" + unit + "}, "
           "\"slices\": [{\"temperatureKelvin\": 293.15, \"points\": [" + points + "]}]}]}";
}

} // namespace

int main() {
    // A valid material: the curve is created and the material validates.
    {
        FatigueMaterial m;
        const auto r = readMaterial(json(material("stressLifeSN",
            "\"symbol\": \"MPa\", \"scaleToSI\": 1e6, \"dimension\": {\"mass\": 1, \"length\": -1, \"time\": -2}")), m);
        assert(r.ok);
        assert(m.name == "steel");
        assert(m.findCurve(FatigueCurveKind::StressLifeSN) != nullptr);
        assert(m.curves.size() == 1 && m.curves[0].slices().size() == 1);
        assert(m.curves[0].amplitudeUnit().dimension.known);
        assert(m.curves[0].amplitudeUnit().dimension.mass == 1 && m.curves[0].amplitudeUnit().dimension.length == -1);
        std::string diagnostic;
        assert(m.validate(diagnostic) == FatigueCurveStatus::Success);
    }

    // Material errors name the member that failed.
    {
        FatigueMaterial m;
        auto r = readMaterial(json(material("sn")), m);
        assert(!r && r.error.find("curves[0].kind: unknown kind 'sn'") != std::string::npos);

        r = readMaterial(json("{\"curves\": []}"), m);
        assert(!r && r.error.find("curves: expected a non-empty array") != std::string::npos);

        r = readMaterial(json("{}"), m);
        assert(!r && r.error.find("missing 'curves'") != std::string::npos);

        r = readMaterial(json(material("stressLifeSN", "\"symbol\": \"MPa\"")), m);
        assert(!r && r.error.find("amplitudeUnit: missing 'scaleToSI'") != std::string::npos);

        r = readMaterial(json(material("stressLifeSN", "\"symbol\": \"MPa\", \"scaleToSI\": 1e6, \"dimension\": {\"mass\": 1.5}")), m);
        assert(!r && r.error.find("dimension.mass: expected an integer exponent") != std::string::npos);

        r = readMaterial(json(material("stressLifeSN", "\"symbol\": \"MPa\", \"scaleToSI\": 1e6, \"dimension\": {\"force\": 1}")), m);
        assert(!r && r.error.find("unknown exponent 'force'") != std::string::npos);

        r = readMaterial(json(material("stressLifeSN", "\"symbol\": \"MPa\", \"scaleToSI\": 1e6", "{\"amplitude\": \"x\", \"cyclesToFailure\": 1}")), m);
        assert(!r && r.error.find("points[0].amplitude: expected a number") != std::string::npos);

        // Points that the curve rejects (life must fall as the amplitude rises) come back as the curve's own message.
        r = readMaterial(json(material("stressLifeSN", "\"symbol\": \"MPa\", \"scaleToSI\": 1e6",
            "{\"amplitude\": 150, \"cyclesToFailure\": 1e4}, {\"amplitude\": 300, \"cyclesToFailure\": 1e6}")), m);
        assert(!r && r.error.find("curves[0]") == 0);
    }

    // Positions.
    {
        ResultPosition p;
        assert(parsePosition("node:12", p).ok && p.location == Fatigue::ResultLocation::Node && p.entityId == 12);
        assert(parsePosition("element:7", p).ok && p.location == Fatigue::ResultLocation::Element && p.entityId == 7);
        assert(parsePosition("ip:7:3", p).ok && p.location == Fatigue::ResultLocation::IntegrationPoint && p.entityId == 7 && p.integrationPoint == 3);
        assert(!parsePosition("node:0", p));
        assert(!parsePosition("node:-1", p));
        assert(!parsePosition("node:1x", p));
        assert(!parsePosition("ip:7", p));
        assert(!parsePosition("ip:7:0", p));
        assert(!parsePosition("face:1", p));
        assert(!parsePosition("node", p));
    }

    // Projections are unit directions; zero and malformed vectors are rejected.
    {
        StressProjection s;
        assert(parseProjection("3,0,4", s).ok);
        assert(std::fabs(s.direction[0] - 0.6) < 1e-12 && std::fabs(s.direction[2] - 0.8) < 1e-12);
        assert(!parseProjection("0,0,0", s));
        assert(!parseProjection("1,2", s));
        assert(!parseProjection("1,2,3,4", s));
        assert(!parseProjection("1,x,3", s));
    }

    // Spectrum: defaults, relative order of blocks and projection normalisation.
    {
        SpectrumSpecification sp;
        const auto r = readSpectrum(json(
            "{\"positions\": [{\"position\": \"element:5\", \"blocks\": ["
            "{\"repetitions\": 2, \"results\": [\"a.frd\", \"b.frd\"], \"field\": \"STRESS\", \"projection\": [0, 3, 4]},"
            "{\"results\": [\"c.frd\"], \"field\": \"STRESS\"}]}]}"), sp);
        assert(r.ok);
        assert(sp.temperatureKelvin == 293.15);
        assert(sp.positions.size() == 1);
        const auto& pos = sp.positions[0];
        assert(pos.label == "element:5" && pos.position.location == Fatigue::ResultLocation::Element && pos.position.entityId == 5);
        assert(pos.blocks.size() == 2);
        assert(pos.blocks[0].repetitions == 2.0 && pos.blocks[0].results.size() == 2 && pos.blocks[0].results[1] == "b.frd");
        assert(pos.blocks[1].repetitions == 1.0 && pos.blocks[1].label == "block 2");
        assert(std::fabs(pos.blocks[0].projection.direction[1] - 0.6) < 1e-12);
        assert(pos.blocks[1].projection.direction[0] == 1.0);
    }

    // Spectrum errors.
    {
        SpectrumSpecification sp;
        auto r = readSpectrum(json("{\"positions\": []}"), sp);
        assert(!r && r.error.find("positions: expected a non-empty array") != std::string::npos);

        r = readSpectrum(json("{\"temperatureKelvin\": 0, \"positions\": [{\"position\": \"node:1\", \"blocks\": []}]}"), sp);
        assert(!r && r.error.find("temperatureKelvin: must be positive") != std::string::npos);

        r = readSpectrum(json("{\"positions\": [{\"position\": \"node:x\", \"blocks\": []}]}"), sp);
        assert(!r && r.error.find("positions[0].position: expected node:ID") != std::string::npos);

        r = readSpectrum(json("{\"positions\": [{\"position\": \"node:1\", \"blocks\": [{\"field\": \"S\", \"results\": []}]}]}"), sp);
        assert(!r && r.error.find("blocks[0].results: expected a non-empty array") != std::string::npos);

        r = readSpectrum(json("{\"positions\": [{\"position\": \"node:1\", \"blocks\": [{\"field\": \"S\", \"results\": [\"a\"], \"repetitions\": 0}]}]}"), sp);
        assert(!r && r.error.find("repetitions: must be positive") != std::string::npos);

        r = readSpectrum(json("{\"positions\": [{\"position\": \"node:1\", \"blocks\": [{\"results\": [\"a\"]}]}]}"), sp);
        assert(!r && r.error.find("missing 'field'") != std::string::npos);
    }
    return 0;
}
