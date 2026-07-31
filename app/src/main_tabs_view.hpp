#pragma once

#include <borealis.hpp>
#include "top_bar_frame.hpp"

// MARK: - MainTabsView (Nintendo Switch port, Boosteroid protocol)
//
// Simplified relative to SwitchNOW's original: no periodic background
// re-authentication loop. Boosteroid's cookie-session auth has no CONFIRMED
// refresh-token mechanism yet (see boosteroid_client.hpp's LoadSavedSession
// doc comment and models.hpp's AuthSession.reauthentication_required) — a
// session either still works or it doesn't, and any tab making an
// authenticated call surfaces a failure through its own error handling
// (see LibraryTab::ReloadLibrary) rather than this view polling in the
// background. If Boosteroid's refresh flow ever gets confirmed, port
// SwitchNOW's MaybeRefreshAuthentication loop back in here.
namespace opennow
{

class MainTabsView : public TopBarFrame
{
  public:
    MainTabsView();
};

} // namespace opennow
