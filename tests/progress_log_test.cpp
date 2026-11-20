/**
 * @file progress_log_test.cpp
 * @brief The progress log: sequence numbers, capacity, reading since a sequence and the parsing of Gmsh log lines.
 */
#include <OpenModel/ProgressLog.hpp>

#include <iostream>

using namespace OpenModel;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            ++g_failures;                                                                  \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n";        \
        }                                                                                  \
    } while (0)

} // namespace

int main() {
    ProgressLog& log = ProgressLog::instance();
    log.clear();
    log.setCapacity(5000);
    const std::uint64_t start = log.lastSequence();

    log.add(ProgressLevel::Info, "OpenModel", "first");
    log.add(ProgressLevel::Warning, "Gmsh", "second");
    log.add(ProgressLevel::Error, "TetGen", "third");
    CHECK(log.lastSequence() == start + 3);

    auto all = log.since(start);
    CHECK(all.size() == 3);
    CHECK(all.size() == 3 && all[0].text == "first" && all[2].level == ProgressLevel::Error);
    CHECK(all.size() == 3 && all[1].source == "Gmsh" && all[1].sequence == start + 2);
    CHECK(all.size() == 3 && all[0].timeMilliseconds > 0);

    // Reading after the last sequence gives nothing; reading after the first gives the two others.
    CHECK(log.since(log.lastSequence()).empty());
    CHECK(log.since(start + 1).size() == 2);

    // The capacity drops the oldest entries, the sequence numbers keep increasing.
    log.setCapacity(2);
    auto kept = log.since(0);
    CHECK(kept.size() == 2);
    CHECK(kept.size() == 2 && kept[0].text == "second" && kept[1].text == "third");
    log.add(ProgressLevel::Debug, "OpenModel", "fourth");
    CHECK(log.since(0).size() == 2);
    CHECK(log.lastSequence() == start + 4);

    // clear() empties the log but does not reset the numbering.
    log.clear();
    CHECK(log.since(0).empty());
    CHECK(log.lastSequence() == start + 4);
    log.setCapacity(5000);

    CHECK(progressLevelName(ProgressLevel::Error) == "error");
    CHECK(progressLevelName(ProgressLevel::Debug) == "debug");

    // Gmsh log lines: the prefix gives the level, the rest is the message.
    ProgressLevel level = ProgressLevel::Debug;
    std::string text;
    CHECK(parseGmshLogLine("Info    : Meshing curve 1 (Line)", level, text));
    CHECK(level == ProgressLevel::Info && text == "Meshing curve 1 (Line)");
    CHECK(parseGmshLogLine("Warning : Surface 4 has no interior points", level, text));
    CHECK(level == ProgressLevel::Warning && text == "Surface 4 has no interior points");
    CHECK(parseGmshLogLine("Error   : Mesh generation failed", level, text));
    CHECK(level == ProgressLevel::Error && text == "Mesh generation failed");
    CHECK(parseGmshLogLine("Debug   : tolerance 1e-8", level, text));
    CHECK(level == ProgressLevel::Debug && text == "tolerance 1e-8");
    CHECK(parseGmshLogLine("no prefix here", level, text));
    CHECK(level == ProgressLevel::Info && text == "no prefix here");
    CHECK(!parseGmshLogLine("   ", level, text));

    if (g_failures == 0) std::cout << "progress_log_test: all checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
