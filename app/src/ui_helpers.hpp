#pragma once

#include "boosteroid_client.hpp"
#include "models.hpp"

#include <string>

namespace opennow
{

void ShowDialog(const std::string& title, const std::string& body);
void ShowError(const std::string& title, const std::string& body);

// Enqueues/awaits a Boosteroid session for `game_id` and, once ready, pushes
// StreamView. Shows a progress dialog mirroring SwitchNOW's
// LaunchSessionDialog, mapped onto Boosteroid's simpler EN/UN/LI state
// machine (see models.hpp's SessionInfo and boosteroid_client.hpp's header
// comment) instead of GFN's numeric session-status codes.
void LaunchSessionDialog(
    const BoosteroidClient& client,
    const AuthSession& auth,
    const std::string& game_id,
    const std::string& title,
    const std::string& image_url = "");

} // namespace opennow
