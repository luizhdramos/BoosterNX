#include "ui_helpers.hpp"

#include "app_state.hpp"
#include "cover_image_cache.hpp"
#include "localization.hpp"
#include "play_history.hpp"
#include "stream_settings.hpp"
#include "StreamView.hpp"

#include <borealis.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>

namespace opennow
{
namespace
{

struct LaunchSessionState
{
    std::atomic<bool> running {true};
};

// Reused near-verbatim from SwitchNOW's ui_helpers.cpp (LaunchAnimationView) —
// a purely cosmetic 3-stage progress rail + spinner, no protocol coupling.
class LaunchAnimationView final : public brls::View
{
  public:
    LaunchAnimationView()
    {
        setWidth(620);
        setHeight(168);
    }

    void SetState(int source_stage, float progress)
    {
        visual_stage_ = source_stage <= 1 ? 0 : (source_stage == 2 ? 1 : 2);
        progress_ = std::clamp(progress, 0.04f, 0.98f);
        invalidate();
    }

    void draw(NVGcontext* vg, float x, float y, float width, float height,
              brls::Style style, brls::FrameContext* ctx) override
    {
        (void)style;
        (void)ctx;
        constexpr float kPi = 3.14159265358979323846f;
        const double seconds = std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(seconds * 3.2));
        static constexpr const char* kLabels[] = {"QUEUE", "SETUP", "READY"};
        const float rail_y = y + 42.0f;
        const float first_x = x + 76.0f;
        const float gap = (width - 152.0f) * 0.5f;

        nvgBeginPath(vg);
        nvgRect(vg, first_x, rail_y - 2.0f, gap * 2.0f, 4.0f);
        nvgFillColor(vg, nvgRGB(38, 42, 48));
        nvgFill(vg);

        float completed_width = visual_stage_ * gap;
        if (visual_stage_ < 2)
            completed_width += gap * std::clamp((progress_ - visual_stage_ * 0.32f) / 0.64f, 0.0f, 0.90f);
        else
            completed_width = gap * 2.0f;
        nvgBeginPath(vg);
        nvgRect(vg, first_x, rail_y - 2.0f, completed_width, 4.0f);
        nvgFillColor(vg, nvgRGB(77, 218, 130));
        nvgFill(vg);

        nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        for (int i = 0; i < 3; ++i)
        {
            const float cx = first_x + gap * i;
            const bool complete = i < visual_stage_;
            const bool active = i == visual_stage_;
            if (active)
            {
                nvgBeginPath(vg);
                nvgCircle(vg, cx, rail_y, 29.0f + pulse * 3.0f);
                nvgFillColor(vg, nvgRGBA(77, 218, 130, 25 + static_cast<int>(pulse * 22.0f)));
                nvgFill(vg);
            }
            nvgBeginPath(vg);
            nvgCircle(vg, cx, rail_y, 22.0f);
            nvgFillColor(vg, complete || active ? nvgRGB(77, 218, 130) : nvgRGB(24, 28, 33));
            nvgFill(vg);
            nvgStrokeWidth(vg, 2.0f);
            nvgStrokeColor(vg, complete || active ? nvgRGB(108, 235, 153) : nvgRGB(43, 48, 55));
            nvgStroke(vg);

            nvgFontSize(vg, 17.0f);
            nvgFillColor(vg, complete || active ? nvgRGB(8, 35, 22) : nvgRGB(95, 101, 111));
            const std::string number = std::to_string(i + 1);
            nvgText(vg, cx, rail_y, number.c_str(), nullptr);
            nvgFontSize(vg, 12.0f);
            nvgFillColor(vg, complete || active ? nvgRGB(228, 235, 232) : nvgRGB(91, 96, 105));
            nvgText(vg, cx, rail_y + 42.0f, kLabels[i], nullptr);
        }

        const float spinner_x = x + width * 0.5f;
        const float spinner_y = y + 137.0f;
        const float angle = static_cast<float>(std::fmod(seconds * 3.0, static_cast<double>(kPi) * 2.0));
        nvgBeginPath(vg);
        nvgCircle(vg, spinner_x, spinner_y, 18.0f);
        nvgStrokeWidth(vg, 5.0f);
        nvgStrokeColor(vg, nvgRGB(35, 43, 48));
        nvgStroke(vg);
        nvgBeginPath(vg);
        nvgArc(vg, spinner_x, spinner_y, 18.0f, angle, angle + kPi * 1.42f, NVG_CW);
        nvgStrokeWidth(vg, 5.0f);
        nvgLineCap(vg, NVG_ROUND);
        nvgStrokeColor(vg, nvgRGB(77, 218, 130));
        nvgStroke(vg);
        const float head_angle = angle + kPi * 1.42f;
        nvgBeginPath(vg);
        nvgCircle(vg, spinner_x + std::cos(head_angle) * 18.0f, spinner_y + std::sin(head_angle) * 18.0f, 3.2f);
        nvgFillColor(vg, nvgRGB(123, 242, 166));
        nvgFill(vg);
    }

  private:
    float progress_ = 0.08f;
    int visual_stage_ = 0;
};

void SetLaunchProgress(
    LaunchAnimationView* animation, brls::Label* stage_label, brls::Label* detail_label,
    int stage, const std::string& title, const std::string& detail, float progress)
{
    if (animation)
        animation->SetState(stage, progress);
    if (stage_label)
    {
        stage_label->setText(Tr(title));
        stage_label->setFontSize(27);
        stage_label->setTextColor(nvgRGB(238, 242, 245));
    }
    if (detail_label)
        detail_label->setText(Tr(detail));
}

} // namespace

void ShowDialog(const std::string& title, const std::string& body)
{
    auto* dialog = new brls::Dialog(Tr(title) + "\n\n" + Tr(body));
    dialog->addButton(Tr("Close"), [] {});
    dialog->setCancelable(true);
    dialog->open();
}

void ShowError(const std::string& title, const std::string& body)
{
    auto* dialog = new brls::Dialog(Tr(title) + "\n\n" + Tr(body));
    dialog->addButton(Tr("Close"), [] {});
    dialog->setCancelable(false);
    dialog->open();
}

void LaunchSessionDialog(
    const BoosteroidClient& client, const AuthSession& auth,
    const std::string& game_id, const std::string& title, const std::string& image_url)
{
    auto* box = new brls::Box(brls::Axis::COLUMN);
    box->setWidth(720);
    box->setPadding(24, 36, 18, 36);
    box->setAlignItems(brls::AlignItems::CENTER);
    box->setBackgroundColor(nvgRGB(15, 17, 21));
    box->setCornerRadius(18);

    auto* game_header = new brls::Box(brls::Axis::ROW);
    game_header->setWidth(620);
    game_header->setHeight(96);
    game_header->setAlignItems(brls::AlignItems::CENTER);
    game_header->setMarginBottom(6);

    auto* cover = new brls::Image();
    cover->setWidth(82);
    cover->setHeight(82);
    cover->setCornerRadius(12);
    cover->setScalingType(brls::ImageScalingType::FILL);
    cover->setMarginRight(20);
    SetCachedThumbnailImage(cover, image_url);
    game_header->addView(cover);

    auto* game_copy = new brls::Box(brls::Axis::COLUMN);
    game_copy->setGrow(1.0f);
    auto* eyebrow = new brls::Label();
    eyebrow->setText(Tr("NOW LOADING"));
    eyebrow->setFontSize(13);
    eyebrow->setTextColor(nvgRGB(77, 218, 130));
    eyebrow->setMarginBottom(5);
    game_copy->addView(eyebrow);
    auto* heading = new brls::Label();
    heading->setText(title);
    heading->setFontSize(27);
    heading->setTextColor(nvgRGB(245, 248, 250));
    game_copy->addView(heading);
    game_header->addView(game_copy);
    box->addView(game_header);

    auto* animation = new LaunchAnimationView();
    box->addView(animation);

    auto* stage_label = new brls::Label();
    stage_label->setFontSize(27);
    stage_label->setTextColor(nvgRGB(238, 242, 245));
    stage_label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    stage_label->setMarginBottom(6);
    box->addView(stage_label);

    auto* detail_label = new brls::Label();
    detail_label->setFontSize(16);
    detail_label->setTextColor(nvgRGB(151, 159, 170));
    detail_label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    box->addView(detail_label);

    SetLaunchProgress(animation, stage_label, detail_label, 0,
                      "Checking Boosteroid session", "Enqueueing your machine.", 0.08f);

    auto* dialog = new brls::Dialog(box);
    dialog->setCancelable(false);

    auto launch_state = std::make_shared<LaunchSessionState>();
    dialog->addButton(Tr("Cancel"), [launch_state]() { launch_state->running = false; });
    dialog->open();

    BoosteroidClient bg_client = client;
    AuthSession bg_auth = auth;
    std::string bg_game_id = game_id;
    std::string bg_title = title;

    brls::async([dialog, animation, stage_label, detail_label,
                 bg_client, bg_auth, bg_game_id, bg_title, launch_state]() mutable {
        auto post_progress = [animation, stage_label, detail_label, launch_state](
            int stage, std::string title, std::string detail, float progress) {
            brls::sync([animation, stage_label, detail_label, launch_state,
                        stage, title = std::move(title), detail = std::move(detail), progress]() {
                if (!launch_state->running)
                    return;
                SetLaunchProgress(animation, stage_label, detail_label, stage, title, detail, progress);
            });
        };
        try
        {
            CookieJar cookies(bg_auth.tokens.session_cookies.begin(), bg_auth.tokens.session_cookies.end());
            const StreamSettings settings = LoadStreamSettings();
            SessionCreateRequest request;
            request.game_id = bg_game_id;

            // The realtime queue-position socket's `token` query param needs
            // the RAW access token, no "Bearer " prefix — AuthTokens::access_token
            // stores it WITH that prefix (see models.hpp's doc comment), unlike
            // the `access_token` cookie's own separate encoding. Stripping it
            // here (rather than in boosteroid_client.cpp) keeps that formatting
            // detail a UI-layer concern, matching where AuthSession is held.
            std::string realtime_token = bg_auth.tokens.access_token;
            constexpr const char* kBearerPrefix = "Bearer ";
            if (realtime_token.rfind(kBearerPrefix, 0) == 0)
                realtime_token.erase(0, std::strlen(kBearerPrefix));

            SessionInfo info = bg_client.CreateAndAwaitSession(
                request, cookies,
                [&post_progress, &launch_state](const SessionInfo& s, int attempt) -> bool {
                    if (!launch_state->running.load())
                        return false;
                    if (s.status == "EN")
                    {
                        // queue_position is only ever set from the realtime
                        // WebSocket's push (no REST endpoint reports it), so
                        // it stays -1 until the first one arrives — keep the
                        // old generic wording for that window rather than
                        // showing a meaningless "position -1".
                        if (s.queue_position >= 0)
                        {
                            // "Position in queue:{0}" is an existing key with
                            // translations already present in localization.cpp
                            // (inherited from SwitchNOW's GFN queue UI) — reuse
                            // it rather than introducing an untranslated string.
                            std::string detail = TrFormat("Position in queue:{0}",
                                                          {" " + std::to_string(s.queue_position)});
                            if (s.queue_eta_seconds > 0)
                                detail += "  |  ~" + std::to_string((s.queue_eta_seconds + 59) / 60) + " min";
                            post_progress(1, "Waiting in queue", detail, 0.3f);
                        }
                        else
                        {
                            post_progress(1, "Waiting in queue", "Boosteroid is queueing a machine for you.", 0.3f);
                        }
                    }
                    else if (s.status == "UN" || s.status == "confirmed")
                        post_progress(2, "Setting up your machine", "The machine was reserved and is booting.", 0.6f);
                    else if (s.status == "LI")
                        post_progress(2, "Machine ready", "Connecting to the stream.", 0.85f);
                    else
                        post_progress(1, "Waiting for Boosteroid", s.status, 0.3f + std::min(attempt, 10) * 0.01f);
                    return true;
                },
                60'000, 3'000, 180,
                bg_auth.user.user_id, realtime_token);

            if (!launch_state->running)
                return;
            if (!info.node_base_url || !info.query_string)
                throw BoosteroidClientError("Session became ready but is missing its gateway/queryString. Try again.");

            post_progress(2, "Starting the video stream", "Opening the control channel and negotiating WebRTC.", 0.92f);

            brls::sync([=]() {
                if (!launch_state->running)
                    return;
                dialog->close([launch_state]() { launch_state->running = false; });

                auto& app_state = AppState::Instance();
                if (app_state.HasSession() && app_state.session()->user.user_id == bg_auth.user.user_id)
                    app_state.SetSession(bg_auth);
                const std::string played_at = CurrentUtcIsoTimestamp();
                RecordGamePlayed(bg_game_id, bg_title, played_at);
                app_state.MarkGamePlayed(bg_game_id, bg_title, played_at);

                brls::Application::pushActivity(new brls::Activity(StreamView::create(
                    bg_client, bg_auth, info, settings, bg_title)));
            });
        }
        catch (const std::exception& e)
        {
            std::string err = e.what();
            brls::sync([=]() {
                if (!launch_state->running)
                    return;
                animation->SetState(0, 0.04f);
                stage_label->setText(Tr("Session could not start"));
                stage_label->setTextColor(nvgRGB(255, 125, 132));
                detail_label->setText(err + "\n\nPress B to return to the game page.");
                dialog->setCancelable(true);
            });
        }
    }, false);
}

} // namespace opennow
