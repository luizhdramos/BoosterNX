#pragma once
#include <borealis.hpp>
#include "boosteroid_client.hpp"
#include "models.hpp"
#include "webrtc_session.hpp"
#include "stream_end_policy.hpp"
#include <memory>
#include <chrono>
#include <array>
#ifdef __SWITCH__
#include <switch.h>
#endif

// MARK: - StreamView (Nintendo Switch port)
//
// Simplified relative to SwitchNOW's StreamView: no NTE ("Neverness to
// Everness") game-specific auto-login hack and no GFN free-tier session-limit
// countdown banners (Boosteroid has no CONFIRMED equivalent concept) — see
// _legacy_gfn_reference/StreamView.hpp/.cpp for those. What's kept: the
// per-frame draw/poll loop, npad gamepad polling -> WebRtcSession's
// controller_* methods, an exit combo, and an optional debug stats overlay.
// TODO(port): physical USB/Bluetooth keyboard and mouse input (SwitchNOW's
// VideoSurfaceView-equivalent HID handling) is not wired up yet — controller
// input is Switch's primary input method and was prioritized for this first
// pass.
class StreamView : public brls::Box {
public:
    StreamView(
        const opennow::BoosteroidClient& client,
        const opennow::AuthSession& auth,
        const opennow::SessionInfo& session,
        const opennow::StreamSettings& settings,
        const std::string& game_title);
    ~StreamView() override;

    void draw(NVGcontext* vg, float x, float y, float width, float height, brls::Style style, brls::FrameContext* ctx) override;
    void onFocusGained() override;
    void onFocusLost() override;

    static brls::View* create(
        const opennow::BoosteroidClient& client,
        const opennow::AuthSession& auth,
        const opennow::SessionInfo& session,
        const opennow::StreamSettings& settings,
        const std::string& game_title);

private:
    void ExitStream();
    void DrawConnectingOverlay(NVGcontext* vg, float x, float y, float width, float height);
    void DrawStatsOverlay(NVGcontext* vg, float x, float y);
    void PollGamepads();
    void UpdateStreamEndState(std::chrono::steady_clock::time_point now);

    opennow::BoosteroidClient client_;
    opennow::AuthSession auth_;
    opennow::SessionInfo session_info_;
    opennow::StreamSettings settings_;
    std::string game_title_;
    std::unique_ptr<WebRtcSession> session_;

    bool exit_requested_ = false;
    std::chrono::steady_clock::time_point exit_combo_started_ {};
    bool exit_combo_active_ = false;

    struct GamepadState {
        bool connected = false;
    };
    std::array<GamepadState, 8> gamepads_ {};
#ifdef __SWITCH__
    PadState pad_ {};
#endif

    std::chrono::steady_clock::time_point stream_started_at_ {};
    std::chrono::steady_clock::time_point last_network_check_at_ {};
    bool internet_connected_ = true;
    opennow::StreamEndReason stream_end_reason_ = opennow::StreamEndReason::None;
};
