#include "settings_tab.hpp"

#include "app_state.hpp"
#include "localization.hpp"
#include "stream_settings.hpp"
#include "ui_helpers.hpp"

#include <algorithm>
#include <cstdio>

namespace opennow
{
namespace
{

std::string FormatBitrate(const StreamSettings& settings)
{
    if (settings.automatic_bitrate)
        return "Automatic";
    return std::to_string(settings.manual_bitrate_mbps) + " Mbps";
}

std::string FormatDeadzone(const StreamSettings& settings)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.0f%%", settings.controller_deadzone * 100.0);
    return buffer;
}

} // namespace

SettingsTab::SettingsTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(28, 40, 28, 40);

    auto* header = new brls::Header();
    header->setTitle("Settings");
    header->setSubtitle("Stream quality, controls, interface, and account");
    addView(header);

    auto* category_row = new brls::Box(brls::Axis::ROW);
    category_row->setMarginBottom(16);

    struct CategoryEntry { const char* label; Category category; };
    static constexpr CategoryEntry kCategories[] = {
        {"Account", Category::Account},
        {"Stream", Category::Stream},
        {"Controls", Category::Controls},
        {"Interface", Category::Interface},
        {"Storage", Category::Storage},
    };

    for (const auto& entry : kCategories)
    {
        auto* button = new brls::Button();
        button->setText(Tr(entry.label));
        button->setMarginRight(8);
        Category target = entry.category;
        button->registerClickAction([this, target](brls::View* view) {
            (void)view;
            SelectCategory(target);
            return true;
        });
        category_row->addView(button);
        category_buttons_.push_back(button);
    }
    addView(category_row);

    page_title_ = new brls::Label();
    page_title_->setFontSize(26);
    page_title_->setTextColor(nvgRGB(245, 248, 250));
    page_title_->setMarginBottom(4);
    addView(page_title_);

    page_subtitle_ = new brls::Label();
    page_subtitle_->setFontSize(14);
    page_subtitle_->setTextColor(nvgRGB(151, 159, 170));
    page_subtitle_->setMarginBottom(16);
    addView(page_subtitle_);

    scrolling_frame_ = new brls::ScrollingFrame();
    scrolling_frame_->setGrow(1.0f);
    scrolling_frame_->setScrollingBehavior(brls::ScrollingBehavior::CENTERED);

    content_container_ = new brls::Box(brls::Axis::COLUMN);
    content_container_->setPadding(0, 0, 30, 0);
    scrolling_frame_->setContentView(content_container_);
    addView(scrolling_frame_);

    save_status_ = new brls::Label();
    save_status_->setFontSize(13);
    save_status_->setTextColor(nvgRGB(105, 220, 148));
    addView(save_status_);
}

SettingsTab::~SettingsTab()
{
    alive_->store(false);
}

void SettingsTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (!settings_loaded_)
    {
        saved_settings_ = LoadStreamSettings();
        draft_settings_ = saved_settings_;
        settings_loaded_ = true;
    }
    SelectCategory(category_);
}

void SettingsTab::SelectCategory(Category category)
{
    category_ = category;
    UpdateCategoryChrome();
    RebuildCategory();
}

void SettingsTab::UpdateCategoryChrome()
{
    static constexpr Category kOrder[] = {
        Category::Account, Category::Stream, Category::Controls, Category::Interface, Category::Storage,
    };
    for (size_t i = 0; i < category_buttons_.size(); ++i)
    {
        const bool active = kOrder[i] == category_;
        category_buttons_[i]->setStyle(active ? &brls::BUTTONSTYLE_PRIMARY : &brls::BUTTONSTYLE_DEFAULT);
    }
}

void SettingsTab::RebuildCategory()
{
    content_container_->clearViews();
    option_values_.clear();

    switch (category_)
    {
        case Category::Account: BuildAccountPage(); break;
        case Category::Stream: BuildStreamPage(); break;
        case Category::Controls: BuildControlsPage(); break;
        case Category::Interface: BuildInterfacePage(); break;
        case Category::Storage: BuildStoragePage(); break;
    }
    UpdateOptionValues();
}

brls::Box* SettingsTab::MakeSection(const std::string& title, const std::string& subtitle)
{
    auto* section = new brls::Box(brls::Axis::COLUMN);
    section->setMarginBottom(22);

    auto* title_label = new brls::Label();
    title_label->setText(Tr(title));
    title_label->setFontSize(19);
    title_label->setTextColor(nvgRGB(238, 242, 245));
    title_label->setMarginBottom(subtitle.empty() ? 10 : 2);
    section->addView(title_label);

    if (!subtitle.empty())
    {
        auto* subtitle_label = new brls::Label();
        subtitle_label->setText(Tr(subtitle));
        subtitle_label->setFontSize(13);
        subtitle_label->setTextColor(nvgRGB(140, 148, 158));
        subtitle_label->setMarginBottom(10);
        section->addView(subtitle_label);
    }

    content_container_->addView(section);
    return section;
}

brls::Box* SettingsTab::MakeOptionRow(
    const std::string& title, const std::string& description,
    std::function<std::string()> value, std::function<bool(brls::View*)> action)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setMarginBottom(10);

    auto* copy = new brls::Box(brls::Axis::COLUMN);
    copy->setGrow(1.0f);
    auto* title_label = new brls::Label();
    title_label->setText(Tr(title));
    title_label->setFontSize(16);
    title_label->setTextColor(nvgRGB(238, 242, 245));
    copy->addView(title_label);
    if (!description.empty())
    {
        auto* description_label = new brls::Label();
        description_label->setText(Tr(description));
        description_label->setFontSize(12);
        description_label->setTextColor(nvgRGB(140, 148, 158));
        copy->addView(description_label);
    }
    row->addView(copy);

    auto* button = new brls::Button();
    button->setWidth(220);
    button->registerClickAction(std::move(action));
    row->addView(button);

    option_values_.emplace_back(button, std::move(value));
    content_container_->addView(row);
    return row;
}

brls::Box* SettingsTab::MakeActionRow(
    const std::string& title, const std::string& description, const std::string& button_text,
    std::function<bool(brls::View*)> action, bool destructive)
{
    auto* row = new brls::Box(brls::Axis::ROW);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setMarginBottom(10);

    auto* copy = new brls::Box(brls::Axis::COLUMN);
    copy->setGrow(1.0f);
    auto* title_label = new brls::Label();
    title_label->setText(Tr(title));
    title_label->setFontSize(16);
    title_label->setTextColor(nvgRGB(238, 242, 245));
    copy->addView(title_label);
    if (!description.empty())
    {
        auto* description_label = new brls::Label();
        description_label->setText(Tr(description));
        description_label->setFontSize(12);
        description_label->setTextColor(nvgRGB(140, 148, 158));
        copy->addView(description_label);
    }
    row->addView(copy);

    auto* button = new brls::Button();
    button->setText(Tr(button_text));
    button->setWidth(220);
    if (destructive)
        button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    button->registerClickAction(std::move(action));
    row->addView(button);

    content_container_->addView(row);
    return row;
}

void SettingsTab::UpdateOptionValues()
{
    for (auto& [button, value] : option_values_)
        button->setText(value());
}

void SettingsTab::MarkDirty()
{
    dirty_ = true;
    save_status_->setText(Tr("Unsaved changes"));
    save_status_->setTextColor(nvgRGB(255, 184, 88));
    UpdateOptionValues();
}

void SettingsTab::BuildAccountPage()
{
    MakeSection("Account");
    const auto& state = AppState::Instance();
    if (state.HasSession())
    {
        const auto& user = state.session()->user;
        auto* info = new brls::Label();
        info->setText(Tr("Signed in as") + " " +
            (user.display_name.empty() ? user.email : user.display_name));
        info->setFontSize(16);
        info->setMarginBottom(14);
        content_container_->addView(info);

        MakeActionRow(
            "Sign out", "Removes the saved Boosteroid session from this Switch.",
            "Sign out",
            [this](brls::View* view) { return SignOut(view); },
            true);
    }
    else
    {
        auto* info = new brls::Label();
        info->setText(Tr("Not signed in. Use the Library tab to sign in with your Boosteroid account."));
        info->setFontSize(16);
        content_container_->addView(info);
    }
}

void SettingsTab::BuildStreamPage()
{
    MakeSection("Video", "Applies the next time you launch a game.");

    MakeOptionRow(
        "Resolution", "",
        [this]() {
            for (const auto& option : ResolutionOptions())
                if (option.width == draft_settings_.width && option.height == draft_settings_.height)
                    return option.label;
            return std::to_string(draft_settings_.width) + "x" + std::to_string(draft_settings_.height);
        },
        [this](brls::View* view) { return CycleResolution(view); });

    MakeOptionRow(
        "Frame rate", "",
        [this]() { return std::to_string(draft_settings_.fps) + " fps"; },
        [this](brls::View* view) { return CycleFrameRate(view); });

    MakeOptionRow(
        "Bitrate", "Automatic follows Boosteroid's own resolution-based ladder.",
        [this]() { return FormatBitrate(draft_settings_); },
        [this](brls::View* view) { return CycleBitrate(view); });

    MakeOptionRow(
        "Video backend", "Auto prefers hardware (Deko3D/NVDEC) with a software fallback.",
        [this]() { return draft_settings_.video_backend; },
        [this](brls::View* view) { return CycleVideoBackend(view); });

    MakeSection("Diagnostics");

    MakeOptionRow(
        "Debug overlay", "Shows transport/decode state instead of the connecting animation.",
        [this]() { return draft_settings_.debug_diagnostics ? "On" : "Off"; },
        [this](brls::View* view) { return ToggleDebugDiagnostics(view); });

    MakeOptionRow(
        "Stats overlay", "Shows a small live stats readout during streaming.",
        [this]() { return draft_settings_.show_stats_overlay ? "On" : "Off"; },
        [this](brls::View* view) { return ToggleStatsOverlay(view); });

    MakeSection("");
    MakeActionRow(
        "Save changes", "", "Save",
        [this](brls::View* view) { return SaveChanges(view); });
    MakeActionRow(
        "Discard changes", "", "Revert",
        [this](brls::View* view) { return RevertChanges(view); });
}

void SettingsTab::BuildControlsPage()
{
    MakeSection("Controller", "Only local controller input is implemented in this port (see the Status tab).");

    MakeOptionRow(
        "Stick deadzone", "",
        [this]() { return FormatDeadzone(draft_settings_); },
        [this](brls::View* view) { return CycleDeadzone(view); });

    MakeSection("");
    MakeActionRow(
        "Save changes", "", "Save",
        [this](brls::View* view) { return SaveChanges(view); });
    MakeActionRow(
        "Discard changes", "", "Revert",
        [this](brls::View* view) { return RevertChanges(view); });
}

void SettingsTab::BuildInterfacePage()
{
    MakeSection("Language");
    MakeOptionRow(
        "Interface language", "",
        [this]() { return InterfaceLanguageLabel(draft_settings_.interface_language); },
        [this](brls::View* view) { return ChooseInterfaceLanguage(view); });

    MakeSection("");
    MakeActionRow(
        "Save changes", "", "Save",
        [this](brls::View* view) { return SaveChanges(view); });
    MakeActionRow(
        "Discard changes", "", "Revert",
        [this](brls::View* view) { return RevertChanges(view); });
}

void SettingsTab::BuildStoragePage()
{
    MakeSection("Storage");
    auto* info = new brls::Label();
    info->setText(Tr("Stream and interface settings are stored in stream_settings.json on the SD card. The saved Boosteroid login is stored separately (see the Account tab to sign out)."));
    info->setFontSize(14);
    info->setTextColor(nvgRGB(151, 159, 170));
    info->setMarginBottom(14);
    content_container_->addView(info);
}

bool SettingsTab::SaveChanges(brls::View* view)
{
    (void)view;
    if (SaveStreamSettings(draft_settings_))
    {
        saved_settings_ = draft_settings_;
        dirty_ = false;
        save_status_->setText(Tr("Saved"));
        save_status_->setTextColor(nvgRGB(105, 220, 148));
    }
    else
    {
        save_status_->setText(Tr("Could not save settings"));
        save_status_->setTextColor(nvgRGB(255, 125, 132));
    }
    return true;
}

bool SettingsTab::RevertChanges(brls::View* view)
{
    (void)view;
    draft_settings_ = saved_settings_;
    SetInterfaceLanguage(draft_settings_.interface_language);
    dirty_ = false;
    save_status_->setText("");
    UpdateOptionValues();
    return true;
}

bool SettingsTab::CycleResolution(brls::View* view)
{
    (void)view;
    const auto& options = ResolutionOptions();
    if (options.empty())
        return true;
    size_t index = 0;
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i].width == draft_settings_.width && options[i].height == draft_settings_.height)
            index = i;
    index = (index + 1) % options.size();
    draft_settings_.width = options[index].width;
    draft_settings_.height = options[index].height;
    MarkDirty();
    return true;
}

bool SettingsTab::CycleFrameRate(brls::View* view)
{
    (void)view;
    draft_settings_.fps = draft_settings_.fps >= 60 ? 30 : 60;
    MarkDirty();
    return true;
}

bool SettingsTab::CycleBitrate(brls::View* view)
{
    (void)view;
    static constexpr int kSteps[] = {5, 10, 15, 20, 30, 50, 80};
    constexpr size_t kStepCount = sizeof(kSteps) / sizeof(kSteps[0]);
    if (draft_settings_.automatic_bitrate)
    {
        draft_settings_.automatic_bitrate = false;
        draft_settings_.manual_bitrate_mbps = kSteps[0];
    }
    else
    {
        size_t index = 0;
        for (size_t i = 0; i < kStepCount; ++i)
            if (kSteps[i] == draft_settings_.manual_bitrate_mbps)
                index = i;
        if (index + 1 < kStepCount)
        {
            draft_settings_.manual_bitrate_mbps = kSteps[index + 1];
        }
        else
        {
            draft_settings_.automatic_bitrate = true;
        }
    }
    MarkDirty();
    return true;
}

bool SettingsTab::CycleVideoBackend(brls::View* view)
{
    (void)view;
    draft_settings_.video_backend = draft_settings_.video_backend == "Auto" ? "Software" : "Auto";
    MarkDirty();
    return true;
}

bool SettingsTab::ToggleDebugDiagnostics(brls::View* view)
{
    (void)view;
    draft_settings_.debug_diagnostics = !draft_settings_.debug_diagnostics;
    MarkDirty();
    return true;
}

bool SettingsTab::ToggleStatsOverlay(brls::View* view)
{
    (void)view;
    draft_settings_.show_stats_overlay = !draft_settings_.show_stats_overlay;
    MarkDirty();
    return true;
}

bool SettingsTab::CycleDeadzone(brls::View* view)
{
    (void)view;
    double next = draft_settings_.controller_deadzone + 0.05;
    if (next > 0.35 + 1e-6)
        next = 0.05;
    draft_settings_.controller_deadzone = next;
    MarkDirty();
    return true;
}

bool SettingsTab::ChooseInterfaceLanguage(brls::View* view)
{
    (void)view;
    const auto& options = InterfaceLanguageOptions();
    if (options.empty())
        return true;
    size_t index = 0;
    for (size_t i = 0; i < options.size(); ++i)
        if (options[i].code == draft_settings_.interface_language)
            index = i;
    index = (index + 1) % options.size();
    draft_settings_.interface_language = options[index].code;
    SetInterfaceLanguage(draft_settings_.interface_language);
    MarkDirty();
    return true;
}

bool SettingsTab::SignOut(brls::View* view)
{
    (void)view;
    auto* dialog = new brls::Dialog(Tr("Sign out of Boosteroid on this Switch?"));
    dialog->addButton(Tr("Cancel"), [] {});
    dialog->addButton(Tr("Sign out"), [this] {
        client_.ClearSavedSession();
        AppState::Instance().ClearSession();
        RebuildCategory();
        brls::Application::notify(Tr("Signed out"));
    });
    dialog->setCancelable(true);
    dialog->open();
    return true;
}

} // namespace opennow
