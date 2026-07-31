#include "StreamView.hpp"

#include "localization.hpp"
#include "network_utils.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "ui_helpers.hpp"

#include <algorithm>
#include <cmath>

// MARK: - StreamView (Nintendo Switch port, Boosteroid protocol)
//
// This REPLACES an earlier version of this file that was still SwitchNOW's
// original GFN-specific StreamView.cpp verbatim (GfnClient, NTE auto-login,
// swkbd-driven keyboard-typing automation, free-tier session-limit banners,
// StopSession) — none of that is applicable to Boosteroid (see
// StreamView.hpp's header comment and _legacy_gfn_reference/StreamView.cpp
// for the original if any of it is ever needed again). This implementation
// matches the CURRENT StreamView.hpp: constructor takes
// (BoosteroidClient, AuthSession, SessionInfo, StreamSettings, game_title),
// WebRtcSession exposes controller_update()/controller_connect() (not
// send_gamepad_input()), and there is no cloud-session-stop RPC to call on
// exit (see boosteroid_client.hpp's HangUpSession doc comment — best-effort
// only, not wired up here yet).

StreamView::StreamView(
    const opennow::BoosteroidClient& client,
    const opennow::AuthSession& auth,
    const opennow::SessionInfo& session,
    const opennow::StreamSettings& settings,
    const std::string& game_title)
    : brls::Box(brls::Axis::COLUMN),
      client_(client), auth_(auth), session_info_(session), settings_(settings), game_title_(game_title) {
    setGrow(1.0f);
    setBackgroundColor(nvgRGB(0, 0, 0));
    setFocusable(true);

#ifdef __SWITCH__
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad_);
#endif

    // node_base_url/query_string are ALWAYS populated by the time StreamView
    // is constructed: LaunchSessionDialog (ui_helpers.cpp) only pushes this
    // activity once CreateAndAwaitSession has returned a session whose status
    // is "LI" with both fields set (see boosteroid_client.hpp's
    // FetchSessionDetails doc comment) — a missing value here would be a
    // caller bug, not a recoverable runtime state, so this deliberately does
    // not soft-fail.
    const opennow::CookieJar cookies(auth_.tokens.session_cookies.begin(), auth_.tokens.session_cookies.end());
    session_ = std::make_unique<WebRtcSession>(
        *session.node_base_url, session.session_id, *session.query_string, cookies, settings_);
    session_->start();

    const auto now = std::chrono::steady_clock::now();
    stream_started_at_ = now;
    last_network_check_at_ = now;
}

StreamView::~StreamView() {
    if (session_)
        session_->stop();
}

brls::View* StreamView::create(
    const opennow::BoosteroidClient& client,
    const opennow::AuthSession& auth,
    const opennow::SessionInfo& session,
    const opennow::StreamSettings& settings,
    const std::string& game_title) {
    return new StreamView(client, auth, session, settings, game_title);
}

void StreamView::onFocusGained() { brls::Box::onFocusGained(); }
void StreamView::onFocusLost() { brls::Box::onFocusLost(); }

void StreamView::ExitStream() {
    if (exit_requested_)
        return;
    exit_requested_ = true;
    if (session_)
        session_->stop();
    // Best-effort only: Boosteroid has no CONFIRMED REST/WS teardown call this
    // port implements yet (see boosteroid_client.hpp's HangUpSession doc
    // comment) — the session expires on its own server-side inactivity
    // timeout in the meantime, same caveat BoosteroidATV documents.
    brls::sync([]() { brls::Application::popActivity(); });
}

void StreamView::PollGamepads() {
    if (!session_)
        return;
#ifdef __SWITCH__
    padUpdate(&pad_);
    const bool connected = padIsConnected(&pad_);
    if (connected != gamepads_[0].connected) {
        gamepads_[0].connected = connected;
        if (connected)
            session_->controller_connect(0, "Switch Controller");
        else
            session_->controller_disconnect(0);
    }
    if (!connected)
        return;

    const u64 down = padGetButtons(&pad_);
    // CONFIRMED Boosteroid button indices (see boosteroid_control_channel.hpp
    // and InputSender.swift's pollGamepad, which this mirrors):
    // 0=A 1=B 2=X 3=Y 4=LB 5=RB 6=Back 7=Start 8=LSclick 9=RSclick — an
    // Xbox-style layout on the wire, physically opposite the Switch's A/B and
    // X/Y placement, so A/B and X/Y are swapped here to keep the on-screen
    // prompts matching where the buttons physically sit.
    std::array<bool, 10> buttons {};
    buttons[1] = down & HidNpadButton_A;
    buttons[0] = down & HidNpadButton_B;
    buttons[3] = down & HidNpadButton_X;
    buttons[2] = down & HidNpadButton_Y;
    buttons[4] = down & HidNpadButton_L;
    buttons[5] = down & HidNpadButton_R;
    buttons[6] = down & HidNpadButton_Minus;
    buttons[7] = down & HidNpadButton_Plus;
    buttons[8] = down & HidNpadButton_StickL;
    buttons[9] = down & HidNpadButton_StickR;

    const HidAnalogStickState left = padGetStickPos(&pad_, 0);
    const HidAnalogStickState right = padGetStickPos(&pad_, 1);
    const auto apply_deadzone = [this](float value) {
        const float deadzone = static_cast<float>(settings_.controller_deadzone);
        const float magnitude = std::fabs(value);
        if (magnitude <= deadzone)
            return 0.0f;
        const float sign = value < 0.0f ? -1.0f : 1.0f;
        return sign * std::min(1.0f, (magnitude - deadzone) / (1.0f - deadzone));
    };
    std::array<float, 6> axes {};
    axes[0] = apply_deadzone(std::clamp(left.x / 32767.0f, -1.0f, 1.0f));
    axes[1] = apply_deadzone(std::clamp(-left.y / 32767.0f, -1.0f, 1.0f)); // Boosteroid's Y axis is +1-down; npad's is +1-up.
    axes[2] = (down & HidNpadButton_ZL) ? 1.0f : 0.0f;     // Joy-Con/Pro Controller triggers are digital, not analog.
    axes[3] = apply_deadzone(std::clamp(right.x / 32767.0f, -1.0f, 1.0f));
    axes[4] = apply_deadzone(std::clamp(-right.y / 32767.0f, -1.0f, 1.0f));
    axes[5] = (down & HidNpadButton_ZR) ? 1.0f : 0.0f;

    int hat = 0;
    if (down & HidNpadButton_Up) hat |= 1;
    if (down & HidNpadButton_Right) hat |= 2;
    if (down & HidNpadButton_Down) hat |= 4;
    if (down & HidNpadButton_Left) hat |= 8;

    session_->controller_update(0, buttons, axes, hat);

    // Exit combo: hold L+R+ZL+ZR+Minus for ~900ms. TODO(port): only a single
    // merged npad is polled here (padInitializeDefault), so this is
    // effectively single-controller — real per-player multi-controller
    // support (hidGetNpadStyleSet + one PadState per player, like
    // SwitchNOW's controller handling) is future work, not needed for a
    // cloud-streaming client where one local player is the common case.
    constexpr u64 kExitMask = HidNpadButton_L | HidNpadButton_R | HidNpadButton_ZL | HidNpadButton_ZR | HidNpadButton_Minus;
    const bool combo_down = (down & kExitMask) == kExitMask;
    const auto now = std::chrono::steady_clock::now();
    if (combo_down && !exit_combo_active_) {
        exit_combo_active_ = true;
        exit_combo_started_ = now;
    } else if (!combo_down) {
        exit_combo_active_ = false;
    } else if (exit_combo_active_ && now - exit_combo_started_ >= std::chrono::milliseconds(900)) {
        ExitStream();
    }
#endif
}

void StreamView::UpdateStreamEndState(std::chrono::steady_clock::time_point now) {
    if (now - last_network_check_at_ >= std::chrono::seconds(3)) {
        last_network_check_at_ = now;
        internet_connected_ = opennow::NetworkUtils::HasInternetConnection();
    }

    opennow::StreamEndSignals signals;
    signals.internet_connected = internet_connected_;
    signals.video_started = session_ && session_->got_video_track();
    signals.session_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - stream_started_at_);
    // TODO(port): peer_terminal/signaling_connected/video_idle are left at
    // their defaults (no terminal state, connected, zero idle) — WebRtcSession
    // does not yet surface a structured PeerConnectionState -> PeerTerminalKind
    // mapping or a "time since last decoded frame" duration the way
    // SwitchNOW's original StreamView read them. The only end conditions this
    // currently detects are internet loss (once video has started) and the
    // free-tier clause (inert here, Boosteroid has no CONFIRMED equivalent).
    // Practically, session/control-channel failures are still caught by
    // is_control_channel_alive() surfacing in DrawConnectingOverlay.

    const opennow::StreamEndReason reason = opennow::DetectStreamEnd(signals);
    if (reason != opennow::StreamEndReason::None && stream_end_reason_ == opennow::StreamEndReason::None) {
        stream_end_reason_ = reason;
        ExitStream();
    }
}

void StreamView::draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) {
    (void)style;
    (void)ctx;
    if (exit_requested_)
        return;

    if (session_)
        session_->poll();
    PollGamepads();
    UpdateStreamEndState(std::chrono::steady_clock::now());

    nvgBeginPath(vg);
    nvgRect(vg, x, y, width, height);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    nvgFill(vg);

    const bool has_video = session_ && session_->got_video_track();
    if (has_video) {
        const int draw_width = static_cast<int>(width);
        const int draw_height = static_cast<int>(height);
        AVFrameHolder::instance().get(
            [this, vg, draw_width, draw_height](AVFrame* frame, uint64_t generation, bool /*reused*/) {
                session_->draw(vg, draw_width, draw_height, frame, generation);
            },
            session_->video_target_rtp_timestamp());
    } else {
        DrawConnectingOverlay(vg, x, y, width, height);
    }

    if (settings_.show_stats_overlay)
        DrawStatsOverlay(vg, x + 24, y + 24);
}

void StreamView::DrawConnectingOverlay(NVGcontext* vg, float x, float y, float width, float height) {
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 26);
    nvgFillColor(vg, nvgRGB(238, 242, 245));
    const std::string title = opennow::Tr("Connecting to") + " " + game_title_;
    nvgText(vg, x + width * 0.5f, y + height * 0.5f - 20, title.c_str(), nullptr);

    nvgFontSize(vg, 16);
    nvgFillColor(vg, nvgRGB(151, 159, 170));
    const std::string state = session_ ? session_->get_debug_info() : "";
    nvgText(vg, x + width * 0.5f, y + height * 0.5f + 16, state.c_str(), nullptr);

    if (!session_ || !session_->is_control_channel_alive()) {
        nvgFontSize(vg, 14);
        nvgFillColor(vg, nvgRGB(255, 150, 150));
        const std::string hint = opennow::Tr("Hold L+R+ZL+ZR+Minus to cancel and go back.");
        nvgText(vg, x + width * 0.5f, y + height * 0.5f + 44, hint.c_str(), nullptr);
    }
}

void StreamView::DrawStatsOverlay(NVGcontext* vg, float x, float y) {
    if (!session_)
        return;
    const std::string info = session_->get_debug_info();
    nvgFontFaceId(vg, brls::Application::getFont(brls::FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontSize(vg, 14);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 160));
    nvgBeginPath(vg);
    nvgRect(vg, x - 8, y - 6, 620, 26);
    nvgFill(vg);
    nvgFillColor(vg, nvgRGB(120, 235, 160));
    nvgText(vg, x, y, info.c_str(), nullptr);
}
