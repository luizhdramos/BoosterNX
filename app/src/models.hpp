#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// MARK: - Boosteroid data models (Nintendo Switch / borealis port)
//
// Ported from BoosteroidATV's Swift models (SessionState.swift, AuthCore.swift),
// which document the CONFIRMED wire protocol against real cloud.boosteroid.com
// traffic. See ../../../BoosteroidATV/CLAUDE.md and that project's Swift
// sources for the full confirmation trail (dates, capture method, TODO(protocol)
// markers for anything not yet observed byte-for-byte). Field names below
// intentionally mirror the Swift structs so the two clients stay easy to
// cross-check.
//
// NOTE: unlike GFN, Boosteroid has ONE login provider (no OAuth device-flow
// choice), ONE library endpoint (no separate public/store catalog confirmed
// yet), and no user-selectable streaming region (the gateway is assigned by
// session/details, not chosen client-side) — so this file is intentionally
// much smaller than SwitchNOW's models.hpp. See _legacy_gfn_reference/ for
// the GFN shapes this replaces.

namespace opennow
{

// MARK: - Auth
//
// CONFIRMED 2026-07-27/28 by capturing the real Android TV app's own traffic:
// POST https://cloud.boosteroid.com/api/v1/auth/login is a direct,
// Turnstile-free email/password login (BoosteroidAuthAPI.login in the tvOS
// project). This SUPERSEDES an older browser-plus-cookie-paste flow that an
// earlier revision of BoosteroidATV's CLAUDE.md still describes — the Swift
// *source* (BoosteroidAuthAPI.swift) is the up-to-date reference, not that
// doc's login section. See boosteroid_client.hpp for the exact request shape.
struct AuthTokens
{
    std::string access_token;  // "Bearer <jwt>", as returned by /auth/login.
    std::string refresh_token;
    // Cookie jar captured from Set-Cookie on /auth/login (access_token,
    // refresh_token, boosteroid_auth, boosteroid_session). The REST API is
    // cookie-session authenticated (Laravel/Sanctum-style), NOT bearer-token
    // authenticated — every call under /api/v1 and /api/v2 needs these, plus
    // matching Origin/Referer headers. See BoosteroidClient's authenticated
    // request helper.
    std::vector<std::pair<std::string, std::string>> session_cookies;
    // Absolute expiry. CONFIRMED: /auth/login's "expires_in" field is
    // misleadingly named — it is an absolute "yyyy-MM-dd HH:mm:ss" UTC
    // timestamp, not a duration.
    std::int64_t expires_at_ms = 0;

    bool IsExpired(std::int64_t now_ms) const { return expires_at_ms > 0 && expires_at_ms <= now_ms; }
    bool IsNearExpiry(std::int64_t now_ms) const
    {
        return expires_at_ms <= 0 || expires_at_ms - now_ms < 10 * 60 * 1000;
    }
};

struct AuthUser
{
    // CONFIRMED: GET /api/v1/user's numeric `id`. Needed beyond display — it's
    // the `uid` query param the queue-position realtime WebSocket requires.
    std::string user_id = "unknown";
    std::string display_name;
    std::string email;
    std::string avatar_url;
    // TODO(protocol): Boosteroid's per-account plan/tier isn't decoded yet
    // (no confirmed field). Kept for UI parity with the tvOS app; always
    // "unknown" today.
    std::string membership_tier = "unknown";
};

struct AuthSession
{
    AuthTokens tokens;
    AuthUser user;
    // Local-only bookkeeping (never sent anywhere): lets the UI show
    // "reconnect required" instead of a raw error after a failed refresh.
    // TODO(protocol): BoosteroidAuthAPI.refresh() has no known server-side
    // implementation yet — a fully expired session currently forces a fresh
    // email/password login rather than transparently refreshing.
    bool reauthentication_required = false;
};

// MARK: - Catalog / Library
//
// CONFIRMED 2026-07-22 live: GET /api/v1/boostore/applications/installed
// ?page=1&paginate=50 is the "my library" list — a standard Laravel
// pagination envelope. No CONFIRMED public/store catalog endpoint exists yet
// (unlike GFN's separate "browse all games" catalog) — this port therefore
// only exposes the installed library, matching BoosteroidATV's HomeView.
struct GameInfo
{
    std::string id;         // Boosteroid's numeric appId, stringified.
    std::string title;
    std::string icon_url;   // CONFIRMED square ~200x200 — use for grid tiles.
    std::string banner_url; // CONFIRMED 2560x1440 (16:9) — hero/banner only, not the grid.
    bool is_installed = false;
    std::string last_played; // Local-only (see play_history.*); Boosteroid doesn't report this.
};

// MARK: - Session lifecycle
//
// CONFIRMED end-to-end 2026-07-22 through 2026-07-24 against real queue
// drains and a genuinely playable session — see boosteroid_client.hpp's
// header comment and ../../../BoosteroidATV/CLAUDE.md for the full trail
// (enqueue -> last-session poll -> queues/start confirmation -> session/start
// v2 claim -> session/details for gw+queryString).
struct SessionInfo
{
    std::string session_id;
    // The per-session WebRTC/control-socket gateway host, e.g.
    // "https://sp7.cloud.boosteroid.com:443". Empty until session/details (or
    // the v2 session/start 201 body) supplies it.
    std::optional<std::string> node_base_url;
    // CONFIRMED values: "EN" (queued/enqueued), "UN" (reserved, assigning a
    // machine), "LI" (live/ready). Anything else is treated as "unknown".
    std::string status;
    // CONFIRMED 2026-07-23: session/details' `data.queryString` JWT is
    // REQUIRED as the control WebSocket's auth (it's not just metadata) — see
    // boosteroid_control_channel.hpp.
    std::optional<std::string> query_string;

    bool IsQueued() const { return status == "EN"; }
    bool IsReserved() const { return status == "UN"; }
    bool IsLive() const { return status == "LI"; }
};

struct SessionCreateRequest
{
    std::string game_id; // Boosteroid appId, stringified.
};

// MARK: - WebRTC signaling (REST, webrtc-streamer-shaped)
//
// CONFIRMED response shapes — see boosteroid_signaling_client.hpp.
struct IceServerInfo
{
    // NOTE: Boosteroid's getIceServers returns ONE entry with a `urls` ARRAY
    // (usually a single "turn:HOST:3478?transport=udp" entry); this flattens
    // that to one IceServerInfo per URL to match libpeer's PeerConfiguration
    // (which takes one `urls` string per ice_servers[] slot).
    std::string url;
    std::string username;
    std::string credential;
};

// MARK: - Stream settings
//
// Simplified relative to SwitchNOW/GFN's preset ladder: Boosteroid has no
// user-selectable region (the gateway is assigned by session/details, not
// chosen client-side) and no confirmed codec choice beyond H.264 over WebRTC
// (see BoosteroidATV's StreamController.swift: HEVC/AV1 only exist on
// Boosteroid's native raw-UDP transport, which this app does not implement —
// see boosteroid_control_channel.hpp's clientType note).
struct StreamSettings
{
    int width  = 1920;
    int height = 1080;
    int fps    = 60;
    // CONFIRMED 2026-07-24 (streaming.js): automatic bitrate follows
    // Boosteroid's own resolution->bitrate ladder; manual is user-chosen
    // 3-80 Mbps. Sent to the server as `stream/bandwidth` (bits/sec) over the
    // control channel, not via SDP. See TargetBitrateBps below.
    bool automatic_bitrate     = true;
    int manual_bitrate_mbps    = 20;
    double controller_deadzone = 0.15;
    bool debug_diagnostics     = false;
    bool show_stats_overlay    = false;
    std::string video_backend  = "Auto"; // "Auto" | "Software" (Deko3D/NVDEC vs FFmpeg SW) — see webrtc_session.hpp.
    std::string interface_language = "en"; // See localization.hpp's InterfaceLanguageOptions() for supported codes.
    // "Adaptive" | "Clarity" | "Original" — cycled by video::NextQualityMode(),
    // resolved to shader/FEC tuning by video::ResolveQualityTuning(). See
    // video_quality_policy.hpp.
    std::string image_quality_mode = "Adaptive";
};

// CONFIRMED 2026-07-24 ladder from streaming.js: <0.9MP 7 / <1.0MP 10 /
// <1.2MP 14 / <1.5MP 17 / <1.9MP 20 / >=1.9MP 24 Mbps. Manual mode clamps the
// user's 3-80 Mbps choice. Mirrors StreamController.targetBitrateBps in the
// tvOS app.
inline int TargetBitrateBps(const StreamSettings& settings, int width, int height)
{
    if (!settings.automatic_bitrate)
    {
        const int clamped = std::min(80, std::max(3, settings.manual_bitrate_mbps));
        return clamped * 1'000'000;
    }
    const long long pixels = static_cast<long long>(width) * static_cast<long long>(height);
    if (pixels < 900'000) return 7'000'000;
    if (pixels < 1'000'000) return 10'000'000;
    if (pixels < 1'200'000) return 14'000'000;
    if (pixels < 1'500'000) return 17'000'000;
    if (pixels < 1'900'000) return 20'000'000;
    return 24'000'000;
}

} // namespace opennow
