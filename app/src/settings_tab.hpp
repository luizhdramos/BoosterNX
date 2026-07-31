#pragma once

#include "boosteroid_client.hpp"
#include "models.hpp"

#include <borealis.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// MARK: - SettingsTab (Nintendo Switch port, Boosteroid protocol)
//
// Dramatically simplified relative to SwitchNOW's original (56KB, 7
// categories including per-region server selection, GFN game-language
// choice, and a full audio-pipeline settings page): Boosteroid has no
// user-selectable streaming region (session/details assigns the gateway
// server-side — see models.hpp) and no CONFIRMED per-session language
// parameter. What's left: Account (sign-out), Stream (resolution/fps/
// bitrate/video backend + diagnostics toggles), Controls (deadzone),
// Interface (language), and Storage (clear saved login). See
// _legacy_gfn_reference/settings_tab.cpp for the GFN original.
namespace opennow
{

class SettingsTab : public brls::Box
{
  public:
    SettingsTab();
    ~SettingsTab() override;

    void willAppear(bool resetState) override;

  private:
    enum class Category
    {
        Account,
        Stream,
        Controls,
        Interface,
        Storage,
    };

    void SelectCategory(Category category);
    void RebuildCategory();
    void BuildAccountPage();
    void BuildStreamPage();
    void BuildControlsPage();
    void BuildInterfacePage();
    void BuildStoragePage();
    void UpdateCategoryChrome();
    void UpdateOptionValues();
    void MarkDirty();
    bool SaveChanges(brls::View* view);
    bool RevertChanges(brls::View* view);
    bool CycleResolution(brls::View* view);
    bool CycleFrameRate(brls::View* view);
    bool CycleBitrate(brls::View* view);
    bool CycleVideoBackend(brls::View* view);
    bool ToggleDebugDiagnostics(brls::View* view);
    bool ToggleStatsOverlay(brls::View* view);
    bool CycleDeadzone(brls::View* view);
    bool ChooseInterfaceLanguage(brls::View* view);
    bool SignOut(brls::View* view);

    brls::Box* MakeSection(const std::string& title, const std::string& subtitle = {});
    brls::Box* MakeOptionRow(
        const std::string& title,
        const std::string& description,
        std::function<std::string()> value,
        std::function<bool(brls::View*)> action);
    brls::Box* MakeActionRow(
        const std::string& title,
        const std::string& description,
        const std::string& button_text,
        std::function<bool(brls::View*)> action,
        bool destructive = false);

    BoosteroidClient client_;
    brls::Label* page_title_ = nullptr;
    brls::Label* page_subtitle_ = nullptr;
    brls::Label* save_status_ = nullptr;
    brls::ScrollingFrame* scrolling_frame_ = nullptr;
    brls::Box* content_container_ = nullptr;
    std::vector<brls::Button*> category_buttons_;
    std::vector<std::pair<brls::Button*, std::function<std::string()>>> option_values_;
    Category category_ = Category::Stream;
    StreamSettings saved_settings_;
    StreamSettings draft_settings_;
    bool settings_loaded_ = false;
    bool dirty_ = false;
    std::shared_ptr<std::atomic_bool> alive_ = std::make_shared<std::atomic_bool>(true);
};

} // namespace opennow
