#include <OpenModel/ProgressLog.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>

namespace OpenModel {

std::string progressLevelName(ProgressLevel level) {
    switch (level) {
        case ProgressLevel::Error: return "error";
        case ProgressLevel::Warning: return "warning";
        case ProgressLevel::Info: return "info";
        case ProgressLevel::Debug: return "debug";
    }
    return "info";
}

bool parseGmshLogLine(const std::string& line, ProgressLevel& level, std::string& text) {
    std::size_t begin = 0;
    while (begin < line.size() && std::isspace(static_cast<unsigned char>(line[begin]))) ++begin;
    if (begin >= line.size()) return false;
    const std::size_t colon = line.find(':', begin);
    level = ProgressLevel::Info;
    std::string prefix = colon == std::string::npos ? std::string() : line.substr(begin, colon - begin);
    while (!prefix.empty() && std::isspace(static_cast<unsigned char>(prefix.back()))) prefix.pop_back();
    if (prefix == "Error") level = ProgressLevel::Error;
    else if (prefix == "Warning") level = ProgressLevel::Warning;
    else if (prefix == "Debug") level = ProgressLevel::Debug;
    std::size_t start = colon == std::string::npos ? begin : colon + 1;
    while (start < line.size() && std::isspace(static_cast<unsigned char>(line[start]))) ++start;
    text = line.substr(start);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();
    return true;
}

ProgressLog& ProgressLog::instance() {
    static ProgressLog log;
    return log;
}

void ProgressLog::add(ProgressLevel level, const std::string& source, const std::string& text) {
    ProgressEntry entry;
    entry.level = level;
    entry.source = source;
    entry.text = text;
    entry.timeMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::lock_guard<std::mutex> lock(m_mutex);
    entry.sequence = ++m_lastSequence;
    m_entries.push_back(std::move(entry));
    while (m_entries.size() > m_capacity) m_entries.pop_front();
}

std::vector<ProgressEntry> ProgressLog::since(std::uint64_t after) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<ProgressEntry> result;
    for (const auto& entry : m_entries) {
        if (entry.sequence > after) result.push_back(entry);
    }
    return result;
}

std::uint64_t ProgressLog::lastSequence() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_lastSequence;
}

void ProgressLog::setCapacity(std::size_t capacity) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_capacity = std::max<std::size_t>(1, capacity);
    while (m_entries.size() > m_capacity) m_entries.pop_front();
}

void ProgressLog::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

} // namespace OpenModel
