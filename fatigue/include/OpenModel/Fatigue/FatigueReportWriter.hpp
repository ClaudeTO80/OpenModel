#pragma once

#include <OpenModel/Fatigue/FatigueReport.hpp>
#include <OpenModel/Fatigue/FatigueSpectrumReport.hpp>
#include <OpenModel/Fatigue/FatigueDamageMap.hpp>

#include <iosfwd>
#include <string>

namespace OpenModel {
namespace Fatigue {

enum class FatigueReportWriteStatus { Success, InvalidReport, StreamError };

struct FatigueReportWriteResult {
    FatigueReportWriteStatus status = FatigueReportWriteStatus::InvalidReport;
    std::string diagnostic;
    explicit operator bool() const noexcept { return status == FatigueReportWriteStatus::Success; }
};

/**
 * Dependency-free report serialization. CSV summary and cycle details are
 * separate streams/files so large cycle tables do not inflate the summary.
 * JSON contains the complete per-position analysis and optional cycle arrays.
 */
class FatigueReportWriter final {
public:
    static FatigueReportWriteResult writeCsvSummary(
        const FatigueReport& report, std::ostream& output);
    static FatigueReportWriteResult writeCsvCycles(
        const FatigueReport& report, std::ostream& output);
    static FatigueReportWriteResult writeJson(
        const FatigueReport& report, std::ostream& output,
        bool includeCycles = false);

    static FatigueReportWriteResult writeCsvSpectrumSummary(
        const FatigueSpectrumReport& report, std::ostream& output);
    static FatigueReportWriteResult writeCsvSpectrumBlocks(
        const FatigueSpectrumReport& report, std::ostream& output);
    static FatigueReportWriteResult writeSpectrumJson(
        const FatigueSpectrumReport& report, std::ostream& output,
        bool includeCycles = false);

    static FatigueReportWriteResult writeDamageMapCsv(
        const FatigueDamageMap& map, std::ostream& output);
    static FatigueReportWriteResult writeDamageMapJson(
        const FatigueDamageMap& map, std::ostream& output);
};

} // namespace Fatigue
} // namespace OpenModel
