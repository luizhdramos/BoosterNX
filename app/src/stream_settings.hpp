#pragma once

#include "models.hpp"

#include <string>
#include <vector>

// Persistence + presets for opennow::StreamSettings (models.hpp). Simplified
// relative to SwitchNOW/GFN's stream_settings.hpp: no per-region server list
// (Boosteroid's gateway is assigned by session/details, not user-selected —
// see models.hpp), no "game language" (that was GFN's cloud-session keyboard
// layout parameter; Boosteroid's session API takes no such field in the
// CONFIRMED enqueue/session/start bodies), and only ONE confirmed codec
// (H.264 over WebRTC).
namespace opennow
{

struct ResolutionOption
{
    std::string label;
    int width = 0;
    int height = 0;
};

const std::vector<ResolutionOption>& ResolutionOptions();
StreamSettings LoadStreamSettings();
bool SaveStreamSettings(const StreamSettings& settings);
std::string FormatStreamSettings(const StreamSettings& settings);

} // namespace opennow
