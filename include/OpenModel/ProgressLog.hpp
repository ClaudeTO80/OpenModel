#pragma once

/**
 * @file ProgressLog.hpp
 * @brief Progress log of the running operations, read by the desktop workbench.
 *
 * The messages that the meshers and the import write to the console are also recorded here, with a sequence number,
 * so that a front end can ask for the entries it has not shown yet while an operation is still running. The log only
 * keeps the most recent entries. Thread safe: its own mutex, never held while a message is built, so reading it does
 * not wait for an operation.
 */

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace OpenModel {

/** Severity of a message, from the most to the least important. Matches the Gmsh log levels. */
enum class ProgressLevel { Error, Warning, Info, Debug };

/** One recorded message. */
struct ProgressEntry {
    std::uint64_t sequence = 0;      ///< increases by one per message, from 1
    ProgressLevel level = ProgressLevel::Info;
    std::string source;              ///< who wrote it: "OpenModel", "Gmsh", "TetGen", ...
    std::string text;
    std::int64_t timeMilliseconds = 0;  ///< system clock, milliseconds since the epoch
};

/** Name of a level as shown to the user: "error", "warning", "info" or "debug". */
std::string progressLevelName(ProgressLevel level);

/**
 * @brief Parses one line of the Gmsh log ("Info    : text", "Warning : text", ...).
 * @param line the line as Gmsh writes it
 * @param level receives the level; Info when the prefix is not recognised
 * @param text receives the message without the prefix
 * @return false for an empty line
 */
bool parseGmshLogLine(const std::string& line, ProgressLevel& level, std::string& text);

class ProgressLog {
public:
    /** The log shared by the operations of the process. */
    static ProgressLog& instance();

    /** Records a message. Messages beyond the capacity drop the oldest ones. */
    void add(ProgressLevel level, const std::string& source, const std::string& text);

    /** Entries with a sequence number above `after`, oldest first. */
    std::vector<ProgressEntry> since(std::uint64_t after) const;

    /** Sequence number of the most recent message, 0 when none was recorded. */
    std::uint64_t lastSequence() const;

    /** Maximum number of entries kept (at least 1). */
    void setCapacity(std::size_t capacity);

    /** Removes every entry; the sequence numbers keep increasing. */
    void clear();

private:
    ProgressLog() = default;

    mutable std::mutex m_mutex;
    std::deque<ProgressEntry> m_entries;
    std::uint64_t m_lastSequence = 0;
    std::size_t m_capacity = 5000;
};

} // namespace OpenModel
