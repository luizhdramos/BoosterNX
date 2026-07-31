#include "stream_settings.hpp"
#include "app_paths.hpp"
#include "atomic_file_replace.hpp"
#include "json_utils.hpp"
#include "localization.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace opennow::json;

namespace opennow
{
namespace
{

std::string GetSettingsPath()
{
    return AppHomePath() + "/stream_settings.json";
}

std::string ReadTextFile(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return "";
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

StreamSettings Sanitize(StreamSettings settings)
{
    if (settings.width <= 0 || settings.height <= 0)
    {
        settings.width = 1920;
        settings.height = 1080;
    }
    if (settings.fps != 30 && settings.fps != 60)
        settings.fps = 60;
    settings.manual_bitrate_mbps = std::max(3, std::min(80, settings.manual_bitrate_mbps));
    settings.controller_deadzone = std::max(0.0, std::min(0.5, settings.controller_deadzone));
    if (settings.video_backend != "Auto" && settings.video_backend != "Software")
        settings.video_backend = "Auto";
    if (!IsSupportedInterfaceLanguage(settings.interface_language))
        settings.interface_language = "en";
    if (settings.image_quality_mode != "Adaptive" && settings.image_quality_mode != "Clarity" &&
        settings.image_quality_mode != "Original")
        settings.image_quality_mode = "Adaptive";
    return settings;
}

} // namespace

const std::vector<ResolutionOption>& ResolutionOptions()
{
    static const std::vector<ResolutionOption> options = {
        {"1280x720", 1280, 720},
        {"1600x900", 1600, 900},
        {"1920x1080", 1920, 1080},
    };
    return options;
}

StreamSettings LoadStreamSettings()
{
    const std::string body = ReadTextFile(GetSettingsPath());
    if (body.empty())
        return Sanitize(StreamSettings{});

    JsonPtr root = TryLoad(body);
    if (!root || !json_is_object(root.get()))
        return Sanitize(StreamSettings{});

    StreamSettings settings;
    settings.width = GetInteger(root.get(), "width", settings.width);
    settings.height = GetInteger(root.get(), "height", settings.height);
    settings.fps = GetInteger(root.get(), "fps", settings.fps);
    settings.automatic_bitrate = GetBool(root.get(), "automatic_bitrate", settings.automatic_bitrate);
    settings.manual_bitrate_mbps = GetInteger(root.get(), "manual_bitrate_mbps", settings.manual_bitrate_mbps);
    json_t* deadzone = json_object_get(root.get(), "controller_deadzone");
    if (json_is_real(deadzone) || json_is_integer(deadzone))
        settings.controller_deadzone = json_number_value(deadzone);
    settings.debug_diagnostics = GetBool(root.get(), "debug_diagnostics", settings.debug_diagnostics);
    settings.show_stats_overlay = GetBool(root.get(), "show_stats_overlay", settings.show_stats_overlay);
    const std::string backend = GetString(root.get(), "video_backend");
    if (!backend.empty())
        settings.video_backend = backend;
    const std::string language = GetString(root.get(), "interface_language");
    if (!language.empty())
        settings.interface_language = language;
    const std::string quality_mode = GetString(root.get(), "image_quality_mode");
    if (!quality_mode.empty())
        settings.image_quality_mode = quality_mode;
    return Sanitize(settings);
}

bool SaveStreamSettings(const StreamSettings& settings)
{
    PrepareAppStorage();
    const StreamSettings clean = Sanitize(settings);

    JsonPtr root(json_object(), &json_decref);
    json_object_set_new(root.get(), "width", json_integer(clean.width));
    json_object_set_new(root.get(), "height", json_integer(clean.height));
    json_object_set_new(root.get(), "fps", json_integer(clean.fps));
    json_object_set_new(root.get(), "automatic_bitrate", json_boolean(clean.automatic_bitrate));
    json_object_set_new(root.get(), "manual_bitrate_mbps", json_integer(clean.manual_bitrate_mbps));
    json_object_set_new(root.get(), "controller_deadzone", json_real(clean.controller_deadzone));
    json_object_set_new(root.get(), "debug_diagnostics", json_boolean(clean.debug_diagnostics));
    json_object_set_new(root.get(), "show_stats_overlay", json_boolean(clean.show_stats_overlay));
    json_object_set_new(root.get(), "video_backend", json_string(clean.video_backend.c_str()));
    json_object_set_new(root.get(), "interface_language", json_string(clean.interface_language.c_str()));
    json_object_set_new(root.get(), "image_quality_mode", json_string(clean.image_quality_mode.c_str()));

    char* dump = json_dumps(root.get(), JSON_INDENT(2));
    if (!dump)
        return false;

    const std::string path = GetSettingsPath();
    const std::string temporary_path = path + ".tmp";
    std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        std::free(dump);
        return false;
    }
    stream.write(dump, static_cast<std::streamsize>(std::strlen(dump)));
    stream.flush();
    const bool write_ok = stream.good();
    stream.close();
    std::free(dump);
    if (!write_ok)
    {
        std::remove(temporary_path.c_str());
        return false;
    }
    return storage::ReplaceWithTemporaryFile(temporary_path, path);
}

std::string FormatStreamSettings(const StreamSettings& settings)
{
    const int bitrate_mbps = settings.automatic_bitrate
        ? TargetBitrateBps(settings, settings.width, settings.height) / 1'000'000
        : settings.manual_bitrate_mbps;
    return std::to_string(settings.width) + "x" + std::to_string(settings.height) +
           " @ " + std::to_string(settings.fps) + " FPS | " +
           (settings.automatic_bitrate ? "Auto " : "Manual ") + std::to_string(bitrate_mbps) + " Mbps | " +
           "Backend: " + settings.video_backend;
}

} // namespace opennow
