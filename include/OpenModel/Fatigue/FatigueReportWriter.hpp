#pragma once

#include <OpenModel/Fatigue/FatigueReport.hpp>

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
};

} // namespace Fatigue
} // namespace OpenModel
