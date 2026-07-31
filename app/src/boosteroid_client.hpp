#pragma once

#include "http_client.hpp"
#include "models.hpp"

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// MARK: - Boosteroid REST Client (Nintendo Switch port)
//
// Ported from BoosteroidATV's BoosteroidClient.swift + BoosteroidAuthAPI.swift,
// which document the CONFIRMED wire protocol (dates, capture method) in their
// own header comments — read those alongside this file for the full trail.
// This C++ port mirrors the Swift implementation method-for-method rather
// than reinventing the flow, specifically so future protocol updates (e.g. if
// the queues/start token field name changes) can be ported across by diffing
// the two files. TODO(protocol) markers below match ones in the Swift source.
namespace opennow
{

// Cookie-session auth (Laravel/Sanctum-style) — CONFIRMED the REST API needs
// the full cookie set from /auth/login's Set-Cookie headers, not just the
// bearer access_token, plus matching Origin/Referer. See AuthTokens's doc
// comment in models.hpp.
using CookieJar = std::vector<std::pair<std::string, std::string>>;

std::string BuildCookieHeader(const CookieJar& cookies);

class BoosteroidClientError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

// CONFIRMED shape of session/details' HTTP 406 error body for an
// expired/timed-out session: {"data":{"code":"timeout",...}}. Thrown
// distinctly so the UI can show "try again" instead of a raw error body —
// mirrors BoosteroidClientError.sessionTimedOut in the Swift client.
class SessionTimedOutError : public BoosteroidClientError
{
  public:
    SessionTimedOutError() : BoosteroidClientError("Session timed out while waiting in queue. Please try again.") {}
};

class BoosteroidClient
{
  public:
    BoosteroidClient();

    // MARK: Auth
    //
    // CONFIRMED 2026-07-27/28 by capturing the real Android TV app's own
    // traffic (Frida SSL-pinning bypass + mitmproxy — see
    // ../../../BoosteroidATV/tools/android-tv-capture/): a direct,
    // Turnstile-free email/password login via POST /api/v1/auth/login. This
    // is what the official app's own "Sign in Manually" screen does — no
    // in-app browser needed (which matters here too: libnx/devkitA64 has no
    // embedded WebView any more than tvOS does).
    AuthSession Login(const std::string& email, const std::string& password) const;

    // CONFIRMED 2026-07-22: GET /api/v1/user -> 200
    // {"data":{"id":<int>,"name":...,"email":...,"avatar":...}}. `id` is also
    // the WebSocket `uid` param the queue-position realtime socket needs.
    AuthUser FetchCurrentUser(const CookieJar& cookies) const;

    // MARK: Catalog
    //
    // CONFIRMED 2026-07-22 live: GET
    // /api/v1/boostore/applications/installed?page=1&paginate=50 is the "my
    // library" list. Cookies alone are sufficient (same as /api/v1/user).
    std::vector<GameInfo> FetchLibrary(const CookieJar& cookies) const;

    // MARK: Session Lifecycle
    //
    // CONFIRMED END TO END against a real, paying-tier account's queue drain
    // and a genuinely playable session:
    //   1. POST /api/v2/streaming/session/enqueue {"appId": <int>} -> 204.
    //   2. GET /api/v1/streaming/user/last-session -> 200
    //      {"data":{"sessionId","appId","status"}} ("EN" while queued).
    //   3. The realtime socket (see boosteroid_realtime_client, cosmetic-only
    //      here) pushes queues/start {appId, token} once a machine is
    //      reserved -> POST /api/v2/streaming/session/start
    //      {appId, sessionToken} claims it; its 201 body carries
    //      {sessionId, status:"UN", gateways:[{address,...}]}.
    //   4. POST /api/v1/streaming/session/details?sessionId=... (POST-only;
    //      GET -> 405) returns {"data":{"gw","queryString"}} once truly live,
    //      or HTTP 406 {"data":{"code":"timeout"}} if expired/idle.
    // See ../../../BoosteroidATV/CLAUDE.md's "Session start" section for the
    // EN/UN/LI state machine this mirrors, and the RATE LIMITING section
    // before changing any polling interval below.

    // Starts (or resumes) a session for `request.game_id`. Mirrors
    // BoosteroidClient.createSession: resumes a genuinely still-running
    // session for the SAME game, otherwise releases whatever the account
    // currently holds (hangup + dequeue, best-effort) and always enqueues
    // fresh — enqueue orphans any other session, which is the intentional
    // "switch device to this Switch" behavior.
    SessionInfo CreateSession(const SessionCreateRequest& request, const CookieJar& cookies) const;

    // POST /v2/streaming/session/dequeue, no body -> 204. Releases a queue
    // slot. Does NOT reliably clear last-session's stale record — never used
    // to judge success. Best-effort (returns false on any failure).
    bool Dequeue(const CookieJar& cookies) const;

    // Best-effort teardown of a LIVE session via its own node's
    // /webrtc/api/hangup?peerid=...&sessionId=.... UNVERIFIED body shape
    // (mirrors the `call` convention) — see BoosteroidClient.hangUpSession's
    // comment in the Swift source.
    bool HangUpSession(const std::string& session_id, const CookieJar& cookies) const;

    // Confirms a reserved ("UN") machine — the "INICIAR" button's request.
    // CONFIRMED 2026-07-24: v2 only, body {appId, sessionToken} where
    // sessionToken is the `token` field from the realtime socket's
    // queues/start push (see SetPreferredSessionId below); v1 (no token) is a
    // DIFFERENT feature and is refused (400 "Direct session start not
    // allowed."). Returns (http_status, full_response_body) so the caller can
    // read the 201 body's `gateways[0].address` directly — do not truncate it,
    // a truncated body silently fails to parse as JSON.
    struct StartResult
    {
        int status = 0;
        std::string body;
    };
    StartResult StartStreamingSession(int app_id, const std::string& session_token, const CookieJar& cookies) const;

    // Called once the realtime socket's queues/start push names the REAL
    // session (its `token`, a 36-char UUID) and, once StartStreamingSession's
    // 201 body is parsed, the gateway it names — see the CLAUDE.md note that
    // last-session is STALE and must not gate anything once this is known.
    // const: only ever touches the two `mutable` fields below — CreateAndAwaitSession
    // (itself const) now calls these directly from its own realtime-socket
    // on_message handler.
    void SetPreferredSessionId(const std::string& session_id) const;
    void SetPreferredGateway(const std::string& address) const;

    // POST /v1/streaming/session/details?sessionId=... — the only source of
    // `gw`+`queryString` once genuinely live. Retries a few times on an empty
    // 200 body (CONFIRMED transient eventual-consistency race right after
    // another client claims the same session) before giving up. Throws
    // SessionTimedOutError on a confirmed-expired session.
    SessionInfo FetchSessionDetails(const std::string& session_id, const CookieJar& cookies, int retries_on_empty_body = 3) const;

    // Orchestrates enqueue -> poll last-session -> (once queues/start is
    // known via SetPreferredSessionId) poll session/details, mirroring
    // BoosteroidClient.createAndAwaitSession. BLOCKING — call from a
    // background thread (see brls::async usage elsewhere in this app).
    // `on_poll(info, attempt)` fires after every poll for UI progress; return
    // true from it to keep waiting, false to cancel (mirrors the tvOS app's
    // Task-cancellation checks).
    //
    // `realtime_uid`/`realtime_access_token` (both optional, empty = disabled):
    // BUG FIXED 2026-07-31 — until this pass, NOTHING in this project ever
    // called SetPreferredSessionId/StartStreamingSession, so any account that
    // actually hit a real queue ("EN" -> "UN" reserved, needing the v2
    // confirmation) polled last-session forever with no way to proceed
    // (CONFIRMED on real hardware: launch dialog stuck on "Waiting in queue"
    // indefinitely). When both are non-empty, CreateAndAwaitSession now also
    // opens the realtime queue-position WebSocket itself (CONFIRMED URL/auth
    // in BoosteroidATV's CLAUDE.md: wss://cloud.boosteroid.com/ws?uid=<numeric
    // user id>&token=<raw access token, no "Bearer">) and watches for the
    // queues/start push ({appId, token}), calling StartStreamingSession with
    // that token itself and feeding the 201 body's sessionId/gateway into
    // SetPreferredSessionId/SetPreferredGateway — closing the loop that was
    // previously only half-wired. If the realtime socket never delivers a
    // matching push (or these params are left empty), queued sessions now
    // time out with an actionable error instead of hanging forever — see
    // kQueuedTimeoutSeconds in the .cpp.
    SessionInfo CreateAndAwaitSession(
        const SessionCreateRequest& request,
        const CookieJar& cookies,
        std::function<bool(const SessionInfo&, int attempt)> on_poll = {},
        long long queued_poll_interval_ms = 60'000, // CONFIRMED: faster polling here got the account 429-rate-limited (8/15/32 min lockouts). Queue progress comes from the realtime socket; this is only a slow safety net.
        long long setup_poll_interval_ms = 3'000,
        long long setup_timeout_seconds = 180,
        const std::string& realtime_uid = {},
        const std::string& realtime_access_token = {}) const;

    // MARK: Local session persistence
    //
    // Simpler than GFN's multi-account encrypted vault (SwitchNOW's
    // gfn_client.hpp): Boosteroid has one account per login, and what's
    // persisted (session cookies + a bearer token, both short-lived and
    // already equivalent to what a browser keeps in its own cookie jar) has a
    // similar risk profile to stream_settings.json, which this app already
    // stores in plaintext on the SD card. TODO(protocol): revisit if
    // Boosteroid's cookies turn out to be longer-lived / higher-value than
    // observed so far.
    bool LoadSavedSession(AuthSession& session) const;
    void SaveSession(const AuthSession& session) const;
    void ClearSavedSession() const;

  private:
    HttpClient http_client_;
    // mutable: written from within CreateAndAwaitSession's realtime-socket
    // on_message callback, even though CreateAndAwaitSession itself is a
    // const method (it doesn't change the session *request*, just stashes a
    // transient WS-confirmed session id/gateway for its own polling loop to
    // read back a few lines later). SetPreferredSessionId/SetPreferredGateway
    // stay non-const in the public API below since external callers may also
    // want to set these before calling CreateAndAwaitSession.
    mutable std::string preferred_session_id_;
    mutable std::string preferred_gateway_;

    std::optional<SessionInfo> DetailsIfReady(const std::string& session_id, const CookieJar& cookies) const;
    struct LastSession
    {
        std::string session_id;
        int app_id = 0;
        std::string status;
    };
    std::optional<LastSession> FetchLastSession(const CookieJar& cookies) const;
};

} // namespace opennow
