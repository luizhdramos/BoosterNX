#pragma once

#include "boosteroid_client.hpp"
#include "models.hpp"

#include <borealis.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <vector>

// MARK: - LibraryTab (Nintendo Switch port, Boosteroid protocol)
//
// Simplified relative to SwitchNOW's original: no store filter (Boosteroid's
// confirmed library payload has no publisher/available_stores field — see
// models.hpp's GameInfo) and sign-in pushes the new LoginView (email/password)
// instead of a QR+PIN device-flow dialog.
namespace opennow
{

class LibraryTab : public brls::Box
{
  public:
    LibraryTab();
    ~LibraryTab() override;

    void willAppear(bool resetState) override;

  private:
    void EnsureSessionLoaded();
    void UpdateSessionUi();
    void BeginLogin();
    void Logout();
    void ReloadLibrary(bool background = false);
    void BeginSearch();
    void RebuildGrid();
    void LoadMore();
    void CycleSortMode();
    bool OpenGameDialog(size_t index);

    BoosteroidClient client_;
    std::vector<GameInfo> games_;
    brls::Label* account_label_ = nullptr;
    brls::Label* status_label_ = nullptr;
    brls::Button* search_button_ = nullptr;
    brls::Button* sort_button_ = nullptr;
    brls::Button* sign_in_button_ = nullptr;
    brls::Button* refresh_button_ = nullptr;
    brls::Button* logout_button_ = nullptr;
    brls::Button* load_more_button_ = nullptr;
    brls::ScrollingFrame* scrolling_frame_ = nullptr;
    brls::Box* grid_container_ = nullptr;
    bool loading_ = false;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
    std::string search_query_;
    size_t visible_limit_ = 15;
    size_t sort_mode_index_ = 0;
    std::chrono::steady_clock::time_point last_library_sync_ {};
};

} // namespace opennow
