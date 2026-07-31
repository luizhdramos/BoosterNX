// Flight-recorder logging helpers, extracted from SwitchNOW's
// webrtc/diagnostics.cpp (the free functions there were genuinely
// protocol-agnostic; the class-method portion of that file was deeply tied
// to GFN WebRtcSession's member set and was left in
// _legacy_gfn_reference/webrtc_diagnostics.cpp instead of being ported).
#include "internal.hpp"
#include "stream_diagnostics.hpp"

#include <chrono>
#include <fstream>
#include <mutex>

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

namespace
{

std::mutex& StreamLogMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::chrono::steady_clock::time_point& InputLogStartTime()
{
    static auto start = std::chrono::steady_clock::now();
    return start;
}

void EnsureLogDir()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/BoosterNX", 0777);
#endif
}

} // namespace

namespace opennow::webrtc::internal
{

void AppendInputLog(const std::string& line)
{
    if (!opennow::StreamDiagnosticsEnabled())
        return;
    EnsureLogDir();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - InputLogStartTime()).count();
    std::lock_guard<std::mutex> lock(StreamLogMutex());
    std::ofstream stream("sdmc:/switch/BoosterNX/input.log", std::ios::app);
    if (stream.is_open())
        stream << "[+" << elapsed_ms << "ms] " << line << '\n';
}

void AppendStreamLog(const std::string& line)
{
    if (!opennow::StreamDiagnosticsEnabled())
        return;
    EnsureLogDir();
    static const auto log_start = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - log_start).count();

    std::lock_guard<std::mutex> lock(StreamLogMutex());
    std::ofstream stream("sdmc:/switch/BoosterNX/signaling.log", std::ios::app);
    if (stream.is_open())
        stream << "[+" << elapsed_ms << "ms] " << line << '\n';
    std::ofstream trace("sdmc:/switch/BoosterNX/stream_trace.log", std::ios::app);
    if (trace.is_open())
        trace << "[+" << elapsed_ms << "ms] APP " << line << '\n';
}

void ResetStreamTraceLog()
{
    if (!opennow::StreamDiagnosticsEnabled())
        return;
    EnsureLogDir();
    std::lock_guard<std::mutex> lock(StreamLogMutex());
    {
        std::ofstream stream("sdmc:/switch/BoosterNX/signaling.log", std::ios::trunc);
        if (stream.is_open())
            stream << "BoosterNX signaling and control-channel log\nOne file per stream attempt.\n============================\n";
    }
    {
        std::ofstream stream("sdmc:/switch/BoosterNX/stream_trace.log", std::ios::trunc);
        if (stream.is_open())
            stream << "BoosterNX stream trace\nOne file per stream attempt. Safe to send for debugging.\n=========================================================\n";
    }
    {
        InputLogStartTime() = std::chrono::steady_clock::now();
        std::ofstream stream("sdmc:/switch/BoosterNX/input.log", std::ios::trunc);
        if (stream.is_open())
            stream << "BoosterNX input flight recorder\nController/keyboard/mouse samples -> control WebSocket frame.\n==============================================================\n";
    }
}

void AppendTraceLog(const std::string& line)
{
    if (!opennow::StreamDiagnosticsEnabled())
        return;
    EnsureLogDir();
    static const auto trace_start = std::chrono::steady_clock::now();
    const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - trace_start).count();

    std::lock_guard<std::mutex> lock(StreamLogMutex());
    std::ofstream stream("sdmc:/switch/BoosterNX/stream_trace.log", std::ios::app);
    if (stream.is_open())
        stream << "[+" << elapsed_ms << "ms] " << line << '\n';
}

void AppendTraceBlock(const std::string& title, const std::string& body)
{
    if (!opennow::StreamDiagnosticsEnabled())
        return;
    AppendTraceLog("----- " + title + " BEGIN bytes=" + std::to_string(body.size()) + " -----");
    {
        std::lock_guard<std::mutex> lock(StreamLogMutex());
        std::ofstream stream("sdmc:/switch/BoosterNX/stream_trace.log", std::ios::app);
        if (stream.is_open())
            stream << body << (body.empty() || body.back() == '\n' ? "" : "\n");
    }
    AppendTraceLog("----- " + title + " END -----");
}

std::string PreviewText(const std::string& value, size_t max_chars)
{
    if (value.size() <= max_chars)
        return value;
    return value.substr(0, max_chars) + "...";
}

} // namespace opennow::webrtc::internal
