// libpeer log bridge.
//
// libpeer normally fprintf()s its internal logs to stdout, which on a Switch
// goes nowhere — so every failure inside its ICE agent (candidate gathering,
// STUN/TURN exchanges, connectivity checks) was invisible. Building `peer`
// with LOG_REDIRECT=1 (see extern/libpeer/src/CMakeLists.txt) makes it call
// this peer_log() instead, and we forward the interesting lines into
// runtime.log, which is always on and is what gets pulled off the SD card.
//
// Why filtered rather than everything: libpeer at LEVEL_DEBUG logs inside
// peer_connection_loop(), which runs continuously once streaming starts —
// forwarding all of it would flood runtime.log and bury the signal. The
// keyword list below is deliberately narrow: candidate gathering and ICE
// state, which is where the current open bug lives.
#include "runtime_journal.hpp"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

namespace
{

// Hard cap so a pathological loop can never fill the SD card. ICE gathering
// and connection setup produce well under this; anything past it is noise.
constexpr int kMaxForwardedLines = 400;
std::atomic<int> g_forwarded {0};

bool IsInteresting(const char* message)
{
    static const char* kKeywords[] = {
        "STUN", "stun", "TURN", "turn", "candidate", "Candidate",
        "ICE", "ice", "select error", "Failed", "Invalid", "Resolved",
        "binding", "Binding", "socket", "Socket",
    };
    for (const char* keyword : kKeywords)
    {
        if (std::strstr(message, keyword) != nullptr)
            return true;
    }
    return false;
}

} // namespace

extern "C" void peer_log(char* level_tag, const char* file_name, int line_number, const char* fmt, ...)
{
    char message[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    if (!IsInteresting(message))
        return;
    if (g_forwarded.fetch_add(1) >= kMaxForwardedLines)
        return;

    // Only the basename of the source file — the full build path is long and
    // adds nothing.
    const char* base = file_name ? std::strrchr(file_name, '/') : nullptr;
    base = base ? base + 1 : (file_name ? file_name : "?");

    opennow::LogRuntimeEvent(
        "libpeer",
        level_tag ? level_tag : "LOG",
        std::string(base) + ":" + std::to_string(line_number) + " " + message);
}
