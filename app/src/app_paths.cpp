#include "app_paths.hpp"

#include <cerrno>
#include <cstdio>
#include <sys/stat.h>

namespace opennow
{
namespace
{

bool DirectoryExists(const std::string& path)
{
    struct stat info {};
    return stat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

} // namespace

const std::string& AppHomePath()
{
    static const std::string path = "sdmc:/switch/BoosterNX";
    return path;
}

const std::string& LegacyAppHomePath()
{
    static const std::string path = "sdmc:/switch/OpenNOWSwitch";
    return path;
}

// The project's own immediately-prior name (before the 2026-07-31 rename to
// BoosterNX) — same migration purpose as LegacyAppHomePath above, just one
// generation newer. Kept as a distinct constant/check rather than folding
// into LegacyAppHomePath so a future rename can add a third one the same way
// without losing this one.
const std::string& PreviousAppHomePath()
{
    static const std::string path = "sdmc:/switch/BoosteroidSwitch";
    return path;
}

void PrepareAppStorage()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);

    if (!DirectoryExists(AppHomePath()))
    {
        // Preserve saved sessions, credentials, settings, history and cache
        // when upgrading from an older build under either previous name.
        // Most-recent-first: an install that somehow has both an old
        // "BoosteroidSwitch" AND ancient "OpenNOWSwitch" folder should keep
        // the newer one's data.
        if (DirectoryExists(PreviousAppHomePath()))
            std::rename(PreviousAppHomePath().c_str(), AppHomePath().c_str());
        else if (DirectoryExists(LegacyAppHomePath()))
            std::rename(LegacyAppHomePath().c_str(), AppHomePath().c_str());
    }

    mkdir(AppHomePath().c_str(), 0777);
#endif
}

} // namespace opennow
