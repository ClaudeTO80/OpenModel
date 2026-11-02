#include <OpenModel/Fatigue/FatigueReportWriter.hpp>

#include <cassert>
#include <sstream>

using namespace OpenModel;
using namespace OpenModel::Fatigue;

int main() {
    FatigueReport report;
    report.status = FatigueReportStatus::PartialFailure;
    report.fieldName = "STRESS";
    report.source.solverName = "Solver \"A\"";
    report.successfulPositions = 1;
    report.failedPositions = 1;
    report.zeroDamagePositions = 0;
    report.positiveDamagePositions = 1;
    report.hasMaximumDamage = true;
    report.maximumDamageIndex = 0;
    report.maximumDamage = 0.125;

    FatiguePositionResult good;
    good.position.location = Fatigue::ResultLocation::IntegrationPoint;
    good.position.entityId = 77;
    good.position.integrationPoint = 3;
    good.label = "IP \"hotspot\"";
    good.hasDamage = true;
    good.damage = 0.125;
    good.analysis.status = FatigueAnalysisStatus::Success;
    good.analysis.damage.blockLife = 8.0;
    good.analysis.damage.cyclesEvaluated = 2;
    good.analysis.temperatureKelvin = 293.15;
    LoadCycle cycle;
    cycle.start.time = 0.0; cycle.end.time = 1.0;
    cycle.start.value = -10.0; cycle.end.value = 10.0;
    cycle.range = 20.0; cycle.mean = 0.0; cycle.count = 0.5;
    cycle.start.historySampleIndex = 0; cycle.end.historySampleIndex = 1;
    good.analysis.cycleCounting.cycles.push_back(cycle);

    FatiguePositionResult bad;
    bad.position.location = Fatigue::ResultLocation::Element;
    bad.position.entityId = 88;
    bad.analysis.status = FatigueAnalysisStatus::HistoryBuildFailed;
    bad.analysis.diagnostic = "missing \"STRESS\" record";
    report.positions = {good,bad};

    std::ostringstream summary, cycles, compactJson, fullJson;
    assert(FatigueReportWriter::writeCsvSummary(report,summary));
    assert(FatigueReportWriter::writeCsvCycles(report,cycles));
    assert(FatigueReportWriter::writeJson(report,compactJson,false));
    assert(FatigueReportWriter::writeJson(report,fullJson,true));
    assert(summary.str().find("integration_point,77,3") != std::string::npos);
    assert(summary.str().find("missing") != std::string::npos);
    assert(cycles.str().find("20") != std::string::npos);
    assert(compactJson.str().find("openmodel.fatigue-report.v1") != std::string::npos);
    assert(compactJson.str().find("cycles") == std::string::npos);
    assert(fullJson.str().find("cycles") != std::string::npos);
    assert(fullJson.str().find("Solver") != std::string::npos);

    FatigueReport empty;
    std::ostringstream ignored;
    assert(FatigueReportWriter::writeJson(empty,ignored).status ==
           FatigueReportWriteStatus::InvalidReport);
    return 0;
}
