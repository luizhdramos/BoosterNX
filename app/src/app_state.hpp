#pragma once

#include "models.hpp"

#include <optional>
#include <vector>

namespace opennow
{

// Simplified relative to SwitchNOW's AppState: no `providers` (Boosteroid has
// one login, not a choice of NVIDIA partners) and no `public_games` (no
// CONFIRMED public/store catalog endpoint exists yet — see models.hpp).
class AppState
{
  public:
    static AppState& Instance();

    bool HasLibraryGames() const;
    bool IsSessionLoaded() const;
    bool HasSession() const;

    const std::vector<GameInfo>& library_games() const;
    const std::optional<AuthSession>& session() const;

    void SetLibraryGames(std::vector<GameInfo> games);
    void MarkGamePlayed(const std::string& game_id, const std::string& title,
                        const std::string& timestamp);
    void SetSession(AuthSession session);
    void ClearSession();
    void MarkSessionLoaded();

  private:
    std::vector<GameInfo> library_games_;
    std::optional<AuthSession> session_;
    bool session_loaded_ = false;
};

} // namespace opennow
