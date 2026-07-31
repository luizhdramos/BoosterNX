#include "game_detail_view.hpp"

#include "app_state.hpp"
#include "cover_image_cache.hpp"
#include "localization.hpp"
#include "ui_helpers.hpp"

namespace opennow
{

GameDetailData MakeGameDetail(const GameInfo& game)
{
    GameDetailData data;
    data.title = game.title;
    data.game_id = game.id;
    data.icon_url = game.icon_url;
    data.banner_url = game.banner_url;
    data.last_played = game.last_played;
    return data;
}

GameDetailView::GameDetailView(const BoosteroidClient& client, GameDetailData data)
    : brls::Box(brls::Axis::COLUMN), client_(client), data_(std::move(data))
{
    setPadding(28, 40, 28, 40);
    setGrow(1.0f);

    // BUG FIXED 2026-07-31: this view (like every other top-level view in
    // this app) extends plain brls::Box rather than borealis's
    // brls::AppletFrame, which is what normally registers a default B/Back
    // action (see AppletFrame's constructor, applet_frame.cpp). Since
    // nothing here did that, B did nothing on this screen — CONFIRMED on
    // real hardware. Application::popActivity() is a documented no-op (safe,
    // returns false) if this were ever somehow the bottom of the activity
    // stack, so this is safe to register unconditionally.
    registerAction(Tr("Back"), brls::BUTTON_B, [](brls::View* view) {
        (void)view;
        brls::Application::popActivity();
        return true;
    });

    auto* hero = new brls::Image();
    hero->setWidth(brls::Application::windowWidth - 80);
    hero->setHeight(360);
    hero->setCornerRadius(16);
    hero->setScalingType(brls::ImageScalingType::FILL);
    hero->setMarginBottom(20);
    SetCachedCoverImage(hero, data_.banner_url.empty() ? data_.icon_url : data_.banner_url);
    addView(hero);

    auto* title_label = new brls::Label();
    title_label->setText(data_.title);
    title_label->setFontSize(34);
    title_label->setTextColor(nvgRGB(245, 248, 250));
    title_label->setMarginBottom(6);
    addView(title_label);

    if (!data_.last_played.empty())
    {
        auto* last_played_label = new brls::Label();
        last_played_label->setText(Tr("Last played") + ": " + data_.last_played);
        last_played_label->setFontSize(15);
        last_played_label->setTextColor(nvgRGB(151, 159, 170));
        last_played_label->setMarginBottom(18);
        addView(last_played_label);
    }

    auto* play_button = new brls::Button();
    play_button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    play_button->setText(Tr("Play"));
    play_button->setWidth(260);
    play_button->registerClickAction([this](brls::View* view) {
        (void)view;
        Play();
        return true;
    });
    addView(play_button);

    auto* note = new brls::Label();
    note->setText(Tr(
        "Boosteroid's confirmed library data has no description or genre yet, so this page is intentionally minimal."));
    note->setFontSize(13);
    note->setTextColor(nvgRGB(105, 112, 122));
    note->setMarginTop(20);
    addView(note);
}

void GameDetailView::Play()
{
    auto& state = AppState::Instance();
    if (!state.HasSession())
    {
        ShowError("Not signed in", "Sign in to Boosteroid from the Library tab before launching a game.");
        return;
    }

    LaunchSessionDialog(client_, *state.session(), data_.game_id, data_.title, data_.icon_url);
}

} // namespace opennow
