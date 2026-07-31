#pragma once

#include "boosteroid_client.hpp"
#include "models.hpp"

#include <borealis.hpp>

#include <vector>

// MARK: - SearchTab (Nintendo Switch port, Boosteroid protocol)
//
// Simplified relative to SwitchNOW's original: Boosteroid has no CONFIRMED
// public/store catalog endpoint (see models.hpp), so this only searches the
// signed-in user's own library — effectively a second entry point onto the
// same data LibraryTab shows, useful once a library gets large.
namespace opennow
{

class SearchTab : public brls::Box
{
  public:
    SearchTab();

    void willAppear(bool resetState) override;

  private:
    void EnsureSessionLoaded();
    void OpenSearchIme();
    void RebuildResults();
    void LoadMoreResults();
    bool OpenResult(size_t index);

    BoosteroidClient client_;
    std::vector<GameInfo> library_games_;
    brls::Label* status_label_ = nullptr;
    brls::Button* search_button_ = nullptr;
    brls::Button* load_more_button_ = nullptr;
    brls::ScrollingFrame* scrolling_frame_ = nullptr;
    brls::Box* results_container_ = nullptr;
    std::string search_query_;
    size_t visible_limit_ = 30;
    size_t result_count_ = 0;
};

} // namespace opennow
