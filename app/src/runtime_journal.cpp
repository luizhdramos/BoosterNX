#include "runtime_journal.hpp"

#include "app_paths.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <sys/stat.h>

namespace opennow
{
namespace
{

constexpr std::uint64_t kMaximumLogBytes = 512 * 1024;
std::mutex g_runtime_log_mutex;
std::atomic<std::uint64_t> g_next_operation_id {1};
std::string g_app_version = "unknown";

std::uint64_t FileSize(const std::string& path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0
        ? static_cast<std::uint64_t>(info.st_size)
        : 0;
}

std::string Timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm value {};
#ifdef _WIN32
    gmtime_s(&value, &time);
#else
    gmtime_r(&time, &value);
#endif
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000;

    std::ostringstream out;
    out << std::put_time(&value, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << milliseconds << 'Z';
    return out.str();
}

std::string SingleLine(std::string text)
{
    for (char& character : text)
    {
        if (character == '\r' || character == '\n' || character == '\t')
            character = ' ';
    }
    if (text.size() > 700)
        text.resize(700);
    return text;
}

void RotateIfNeeded()
{
    const std::string& path = RuntimeJournalPath();
    if (FileSize(path) < kMaximumLogBytes)
        return;

    const std::string previous = path + ".previous";
    std::remove(previous.c_str());
    std::rename(path.c_str(), previous.c_str());
}

void AppendLine(const std::string& line)
{
    PrepareAppStorage();
    RotateIfNeeded();
    std::ofstream stream(RuntimeJournalPath(), std::ios::app);
    if (stream.is_open())
        stream << line << '\n';
}

} // namespace

const std::string& RuntimeJournalPath()
{
    static const std::string path = AppHomePath() + "/runtime.log";
    return path;
}

std::string SanitizeRuntimeUrl(const std::string& url)
{
    std::string sanitized = url.substr(0, url.find_first_of("?#"));
    const auto scheme = sanitized.find("://");
    if (scheme != std::string::npos)
    {
        const auto authority_begin = scheme + 3;
        const auto authority_end = sanitized.find('/', authority_begin);
        const auto at = sanitized.find('@', authority_begin);
        if (at != std::string::npos &&
            (authority_end == std::string::npos || at < authority_end))
        {
            sanitized.erase(authority_begin, at - authority_begin + 1);
        }
    }
    return SingleLine(sanitized);
}

void InitializeRuntimeJournal(const std::string& app_version)
{
    std::lock_guard<std::mutex> lock(g_runtime_log_mutex);
    g_app_version = app_version.empty() ? "unknown" : app_version;
    AppendLine(
        Timestamp() + " category=app event=start version=" +
        SingleLine(g_app_version));
}

void ShutdownRuntimeJournal()
{
    std::lock_guard<std::mutex> lock(g_runtime_log_mutex);
    AppendLine(
        Timestamp() + " category=app event=clean_shutdown version=" +
        SingleLine(g_app_version));
}

void LogRuntimeEvent(
    const std::string& category,
    const std::string& event,
    const std::string& detail)
{
    std::lock_guard<std::mutex> lock(g_runtime_log_mutex);
    std::string line =
        Timestamp() + " category=" + SingleLine(category) +
        " event=" + SingleLine(event);
    if (!detail.empty())
        line += " detail=" + SingleLine(detail);
    AppendLine(line);
}

std::uint64_t BeginRuntimeOperation(
    const std::string& category,
    const std::string& operation,
    const std::string& detail)
{
    const std::uint64_t id =
        g_next_operation_id.fetch_add(1, std::memory_order_relaxed);
    LogRuntimeEvent(
        category,
        "begin",
        "id=" + std::to_string(id) + " operation=" + operation +
        (detail.empty() ? "" : " " + detail));
    return id;
}

void EndRuntimeOperation(
    std::uint64_t operation_id,
    const std::string& category,
    const std::string& operation,
    const std::string& result,
    const std::string& detail)
{
    LogRuntimeEvent(
        category,
        "end",
        "id=" + std::to_string(operation_id) +
        " operation=" + operation +
        " result=" + result +
        (detail.empty() ? "" : " " + detail));
}

} // namespace opennow
