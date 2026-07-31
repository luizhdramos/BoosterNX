#pragma once

#include "boosteroid_client.hpp"
#include "models.hpp"

#include <borealis.hpp>

#include <string>

// MARK: - GameDetailView (Nintendo Switch port, Boosteroid protocol)
//
// Much smaller than SwitchNOW's original (no store-variant picker, no NTE
// auto-login button, no screenshot carousel): Boosteroid's confirmed library
// endpoint (GET /api/v1/boostore/applications/installed) only returns
// id/name/icon/bannerImage/installed (see models.hpp's GameInfo) — there is
// no confirmed per-game screenshots/description/publisher/genre payload wired
// up yet, and Boosteroid has exactly one way to launch a game (no store
// choice). See _legacy_gfn_reference/game_detail_view.hpp/.cpp for the
// GFN original if any of that is ever revisited.
namespace opennow
{

struct GameDetailData
{
    std::string title;
    std::string game_id;   // Boosteroid appId, stringified.
    std::string icon_url;
    std::string banner_url;
    std::string last_played;
};

class GameDetailView : public brls::Box
{
  public:
    GameDetailView(const BoosteroidClient& client, GameDetailData data);

  private:
    void Play();

    BoosteroidClient client_;
    GameDetailData data_;
};

GameDetailData MakeGameDetail(const GameInfo& game);

} // namespace opennow
