#include "status_tab.hpp"

#include "app_state.hpp"
#include "ui_helpers.hpp"
#include "localization.hpp"

namespace opennow
{
namespace
{

brls::Label* MakeParagraph(const std::string& text, float bottom_margin = 16.0f)
{
    auto* label = new brls::Label();
    label->setText(Tr(text));
    label->setFontSize(18);
    label->setMarginBottom(bottom_margin);
    return label;
}

} // namespace

StatusTab::StatusTab()
    : brls::Box(brls::Axis::COLUMN)
{
    setPadding(28, 40, 28, 40);

    auto* header = new brls::Header();
    header->setTitle("BoosterNX");
    header->setSubtitle("Native Nintendo Switch homebrew port of Boosteroid cloud gaming");
    addView(header);

    addView(MakeParagraph(
        "This is a from-scratch native Switch client for Boosteroid, sharing its borealis/libpeer/FFmpeg foundation with SwitchNOW (the sibling GeForce NOW port) but a completely different session/signaling protocol, ported from the confirmed BoosteroidATV (tvOS) implementation."));

    addView(MakeParagraph(
        "Implemented: direct email/password login (Boosteroid's Android-TV-style /auth/login, no Turnstile), library sync, the full enqueue -> queue -> reserve -> confirm -> live session lifecycle, the control WebSocket that claims sessions and carries all input, and client-offerer WebRTC signaling over Boosteroid's REST API."));

    addView(MakeParagraph(
        "Known scope cuts for this first pass: physical USB/Bluetooth keyboard and mouse input is not wired up (controller input only), only one merged local controller is polled (no true per-player multi-controller), and there is no confirmed session-teardown call (sessions rely on their own server-side inactivity timeout)."));

    addView(MakeParagraph(
        "See the project's CLAUDE.md for the full protocol confirmation trail and TODO(protocol)/TODO(port) markers.", 24.0f));

    auto* architecture_button = new brls::Button();
    architecture_button->setStyle(&brls::BUTTONSTYLE_PRIMARY);
    architecture_button->setText(Tr("Why the port is structured this way"));
    architecture_button->setMarginBottom(14);
    architecture_button->registerClickAction([this](brls::View* view) {
        return OpenArchitectureDialog(view);
    });
    addView(architecture_button);

    auto* cache_button = new brls::Button();
    cache_button->setText(Tr("Show current cache state"));
    cache_button->registerClickAction([](brls::View* view) {
        (void)view;
        const auto& state = AppState::Instance();
        ShowDialog(
            "Shared Cache",
            "Session loaded: " + std::string(state.IsSessionLoaded() ? "yes" : "no") + "\n" +
                "Signed in: " + std::string(state.HasSession() ? "yes" : "no") + "\n" +
                "Library games cached: " + std::to_string(state.library_games().size()));
        return true;
    });
    addView(cache_button);
}

bool StatusTab::OpenArchitectureDialog(brls::View* view)
{
    (void)view;
    ShowDialog(
        "Port Boundary",
        "BoosterNX reuses SwitchNOW's protocol-agnostic infrastructure verbatim: borealis UI, libpeer WebRTC, FFmpeg decode, Deko3D/OpenGL rendering, and the RTP/decode-queue pipeline.\n\n"
        "Everything protocol-specific was rewritten against BoosteroidATV's CONFIRMED wire protocol: direct email/password auth (boosteroid_client), REST-based WebRTC signaling where THIS client is the offerer (boosteroid_signaling_client), and a dedicated control WebSocket that claims the session and carries all input (boosteroid_control_channel) instead of GFN's WebRTC datachannel.\n\n"
        "The old GFN-specific files (auth, catalog, NTE auto-login, QR login, providers) are kept under _legacy_gfn_reference/ for reference and are excluded from the CMake build.");
    return true;
}

} // namespace opennow
