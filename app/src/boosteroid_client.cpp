#include "boosteroid_client.hpp"
#include "app_paths.hpp"
#include "atomic_file_replace.hpp"
#include "json_utils.hpp"
#include "runtime_journal.hpp"
#include "WebSocketClient.hpp"

#include <fstream>
#include <iterator>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <strings.h> // strncasecmp
#include <thread>

using namespace opennow::json;

namespace opennow
{
namespace
{

// MARK: - Confirmed constants
//
// CONFIRMED 2026-07-22: all REST endpoints (catalog, session lifecycle, user)
// live under this one host — no separate API host like GFN's pcs./login./
// games. split domains. The per-session WebRTC/control gateway is a
// DIFFERENT, per-session host (SessionInfo::node_base_url).
constexpr const char* kApiBaseUrl = "https://cloud.boosteroid.com";

// CONFIRMED 2026-07-27 by capturing the official Android TV client's own
// traffic: this is that app's own embedded OAuth-style client id/secret pair,
// identical across every install (client_id 6) — not per-user. Used only by
// the direct /api/v1/auth/login route, which has no Cloudflare Turnstile
// challenge (Turnstile only gates the browser-facing /auth/login PAGE).
constexpr int kClientId = 6;
constexpr const char* kClientSecret = "CDYb8AnfFEeU3p4Rd1A3oGonxMJMe3TdWJwDWSsy";

// Used for the cookie-session REST calls (catalog/session lifecycle/user).
// NOTE: Cloudflare's cf_clearance cookie (if any) and Boosteroid's own
// session check can be tied to the User-Agent active when the cookies were
// issued — keep this in sync with whatever the login call presents.
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";

std::int64_t NowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// UTC calendar -> epoch ms, without relying on timegm() (not guaranteed
// present on devkitA64/newlib). CONFIRMED: /auth/login's "expires_in" is an
// absolute "yyyy-MM-dd HH:mm:ss" UTC timestamp, not a duration.
std::int64_t ParseUtcTimestampMs(const std::string& value)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (std::sscanf(value.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return 0;

    // Days since epoch via a civil-calendar algorithm (Howard Hinnant's
    // days_from_civil), portable and DST/timezone-free since we treat
    // everything as UTC throughout this app.
    const int y = month <= 2 ? year - 1 : year;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (month + (month > 2 ? -3 : 9)) + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;

    const long long seconds = days * 86400LL + hour * 3600LL + minute * 60LL + second;
    return seconds * 1000LL;
}

std::string Trim(const std::string& value)
{
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
        ++begin;
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])))
        --end;
    return value.substr(begin, end - begin);
}

// Parses one or more "Set-Cookie: name=value; Attr=...; Attr2" response
// header lines into (name, value) pairs, ignoring cookie attributes
// (Path/Domain/Expires/...). CONFIRMED cookies of interest: access_token,
// refresh_token, boosteroid_auth, boosteroid_session.
CookieJar ParseSetCookieHeaders(const std::vector<std::string>& headers)
{
    CookieJar cookies;
    for (const std::string& line : headers)
    {
        if (line.size() < 11 || strncasecmp(line.c_str(), "set-cookie:", 11) != 0)
            continue;
        std::string rest = Trim(line.substr(11));
        const size_t semi = rest.find(';');
        const std::string pair = semi == std::string::npos ? rest : rest.substr(0, semi);
        const size_t eq = pair.find('=');
        if (eq == std::string::npos)
            continue;
        cookies.emplace_back(Trim(pair.substr(0, eq)), pair.substr(eq + 1));
    }
    return cookies;
}

std::string Preview(const std::string& body, size_t max_chars = 220)
{
    return body.size() > max_chars ? body.substr(0, max_chars) + "..." : body;
}

} // namespace

std::string BuildCookieHeader(const CookieJar& cookies)
{
    std::string out;
    for (const auto& [name, value] : cookies)
    {
        if (!out.empty())
            out += "; ";
        out += name + "=" + value;
    }
    return out;
}

BoosteroidClient::BoosteroidClient() = default;

namespace
{

// CONFIRMED (BoosteroidClient.swift's authenticatedRequest doc comment): the
// API is cookie-session authenticated, and needs Origin/Referer to match the
// real frontend for Laravel/Sanctum-style backends to honor the session at
// all.
std::vector<std::string> AuthenticatedHeaders(const CookieJar& cookies, bool json_body = false)
{
    std::vector<std::string> headers = {
        "Cookie: " + BuildCookieHeader(cookies),
        std::string("Origin: ") + kApiBaseUrl,
        std::string("Referer: ") + kApiBaseUrl + "/dashboard",
        "Accept: application/json",
    };
    if (json_body)
        headers.push_back("Content-Type: application/json");
    return headers;
}

} // namespace

// MARK: - Auth

AuthSession BoosteroidClient::Login(const std::string& email, const std::string& password) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v1/auth/login";

    JsonPtr body_json(json_object(), &json_decref);
    json_object_set_new(body_json.get(), "client_id", json_integer(kClientId));
    json_object_set_new(body_json.get(), "client_secret", json_string(kClientSecret));
    json_object_set_new(body_json.get(), "email", json_string(Trim(email).c_str()));
    json_object_set_new(body_json.get(), "password", json_string(password.c_str()));
    char* dump = json_dumps(body_json.get(), JSON_COMPACT);
    const std::string body = dump ? dump : "{}";
    if (dump)
        free(dump);

    // CONFIRMED 2026-07-27/28 (real Android TV app capture): the shared
    // desktop-browser User-Agent (needed elsewhere for cookie/Cloudflare
    // compatibility) makes this Turnstile-free native-app route reject the
    // request ("something wrong with your data") — it likely branches on UA
    // to decide whether to demand a Turnstile token. These headers present as
    // that exact captured Android TV client instead. `x-nonce-17: 18211` is
    // CONFIRMED constant across two separate real logins made minutes apart —
    // a fixed build-level fingerprint, not a per-request nonce, but
    // apparently required by a WAF/gateway check regardless.
    std::vector<std::string> headers = {
        "Content-Type: application/json; charset=UTF-8",
        "Accept: application/json",
        "x-nonce-17: 18211",
        "User-Agent: BoosteroidAndroidTVClient v.2.5.10.tv; Android 14; sdk_gphone64_arm64",
        "device-name: emu64a sdk_gphone64_arm64 34",
        "device-uniq-id: ",
        "accept-language: en-US",
        R"(device-info: {"brand":"google","chip":" ","device":"emu64a","hardware":"ranchu","manufacturer":"Google","model":"sdk_gphone64_arm64","name":"UE1A.230829.050","product":"sdk_gphone64_arm64"})",
        "Cookie: boosteroid_entrypoint_source=1;boosteroid_entrypoint_page=1",
    };

    const HttpResponse response = http_client_.Post(url, "BoosteroidAndroidTVClient v.2.5.10.tv; Android 14; sdk_gphone64_arm64", headers, body);
    if (response.status_code != 200)
        throw BoosteroidClientError("Login failed: HTTP " + std::to_string(response.status_code) + ": " + Preview(response.body));

    JsonPtr root = Load(response.body);
    json_t* data = json_object_get(root.get(), "data");
    json_t* user_json = json_object_get(data, "user");
    if (!user_json)
        throw BoosteroidClientError("Login response missing 'data.user': " + Preview(response.body));

    AuthSession session;
    session.user.user_id = std::to_string(GetInteger(user_json, "id"));
    session.user.display_name = GetString(user_json, "name");
    session.user.email = GetString(user_json, "email");
    session.user.avatar_url = GetString(user_json, "avatar");
    session.user.membership_tier = "unknown"; // TODO(protocol): no confirmed tier field yet.

    session.tokens.access_token = GetString(data, "access_token");
    session.tokens.refresh_token = GetString(data, "refresh_token");
    session.tokens.session_cookies = ParseSetCookieHeaders(response.headers);
    const std::int64_t expires_ms = ParseUtcTimestampMs(GetString(data, "expires_in"));
    session.tokens.expires_at_ms = expires_ms > 0 ? expires_ms : NowMs() + 12LL * 60 * 60 * 1000;

    if (session.tokens.session_cookies.empty())
        LogRuntimeEvent("boosteroid.auth", "warning", "login succeeded but no Set-Cookie headers were captured");

    return session;
}

AuthUser BoosteroidClient::FetchCurrentUser(const CookieJar& cookies) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v1/user";
    const HttpResponse response = http_client_.Get(url, kUserAgent, AuthenticatedHeaders(cookies));
    if (response.status_code != 200)
        throw BoosteroidClientError("fetchCurrentUser failed: HTTP " + std::to_string(response.status_code) + ": " + Preview(response.body));

    JsonPtr root = Load(response.body);
    json_t* data = json_object_get(root.get(), "data");
    AuthUser user;
    user.user_id = std::to_string(GetInteger(data, "id"));
    user.display_name = GetString(data, "name");
    user.email = GetString(data, "email");
    user.avatar_url = GetString(data, "avatar");
    user.membership_tier = "unknown";
    return user;
}

// MARK: - Catalog

std::vector<GameInfo> BoosteroidClient::FetchLibrary(const CookieJar& cookies) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v1/boostore/applications/installed?page=1&paginate=50";
    const HttpResponse response = http_client_.Get(url, kUserAgent, AuthenticatedHeaders(cookies));
    if (response.status_code != 200)
        throw BoosteroidClientError("fetchLibrary failed: HTTP " + std::to_string(response.status_code) + ": " + Preview(response.body));

    JsonPtr root = Load(response.body);
    json_t* data = json_object_get(root.get(), "data");
    std::vector<GameInfo> games;
    if (json_is_array(data))
    {
        size_t index = 0;
        json_t* item = nullptr;
        json_array_foreach(data, index, item)
        {
            GameInfo game;
            game.id = std::to_string(GetInteger(item, "id"));
            game.title = GetString(item, "name");
            game.icon_url = GetString(item, "icon");
            game.banner_url = GetString(item, "bannerImage");
            game.is_installed = GetBool(item, "installed", true);
            games.push_back(std::move(game));
        }
    }
    return games;
}

// MARK: - Session Lifecycle

void BoosteroidClient::SetPreferredSessionId(const std::string& session_id) const { preferred_session_id_ = session_id; }
void BoosteroidClient::SetPreferredGateway(const std::string& address) const { preferred_gateway_ = address; }

std::optional<BoosteroidClient::LastSession> BoosteroidClient::FetchLastSession(const CookieJar& cookies) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v1/streaming/user/last-session";
    const HttpResponse response = http_client_.Get(url, kUserAgent, AuthenticatedHeaders(cookies));
    if (response.status_code != 200)
        return std::nullopt;
    JsonPtr root = TryLoad(response.body);
    if (!root)
        return std::nullopt;
    json_t* data = json_object_get(root.get(), "data");
    if (!data)
        return std::nullopt;
    LastSession last;
    last.session_id = GetString(data, "sessionId");
    last.app_id = GetInteger(data, "appId");
    last.status = GetString(data, "status");
    if (last.session_id.empty())
        return std::nullopt;
    return last;
}

bool BoosteroidClient::Dequeue(const CookieJar& cookies) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v2/streaming/session/dequeue";
    const HttpResponse response = http_client_.Post(url, kUserAgent, AuthenticatedHeaders(cookies, true), "");
    return response.status_code == 204;
}

bool BoosteroidClient::HangUpSession(const std::string& session_id, const CookieJar& cookies) const
{
    std::optional<SessionInfo> details;
    try
    {
        details = FetchSessionDetails(session_id, cookies, 0);
    }
    catch (...)
    {
        return false;
    }
    if (!details || !details->node_base_url)
        return false;

    std::ostringstream url;
    url << *details->node_base_url << "/webrtc/api/hangup?peerid="
        << (static_cast<double>(rand()) / RAND_MAX) << "&sessionId=" << session_id;
    const HttpResponse response = http_client_.Post(url.str(), kUserAgent, AuthenticatedHeaders(cookies), "");
    return response.status_code >= 200 && response.status_code < 300;
}

BoosteroidClient::StartResult BoosteroidClient::StartStreamingSession(int app_id, const std::string& session_token, const CookieJar& cookies) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v2/streaming/session/start";
    JsonPtr body_json(json_object(), &json_decref);
    json_object_set_new(body_json.get(), "appId", json_integer(app_id));
    if (!session_token.empty())
        json_object_set_new(body_json.get(), "sessionToken", json_string(session_token.c_str()));
    char* dump = json_dumps(body_json.get(), JSON_COMPACT);
    const std::string body = dump ? dump : "{}";
    if (dump)
        free(dump);

    const HttpResponse response = http_client_.Post(url, kUserAgent, AuthenticatedHeaders(cookies, true), body);
    return StartResult{static_cast<int>(response.status_code), response.body};
}

SessionInfo BoosteroidClient::FetchSessionDetails(const std::string& session_id, const CookieJar& cookies, int retries_on_empty_body) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v1/streaming/session/details?sessionId=" + session_id;
    int last_empty_status = 0;
    for (int attempt = 0; attempt <= retries_on_empty_body; ++attempt)
    {
        const HttpResponse response = http_client_.Post(url, kUserAgent, AuthenticatedHeaders(cookies), "");
        if (response.status_code == 406)
        {
            JsonPtr root = TryLoad(response.body);
            json_t* data = root ? json_object_get(root.get(), "data") : nullptr;
            if (data && GetString(data, "code") == "timeout")
                throw SessionTimedOutError();
            throw BoosteroidClientError("session/details 406: " + Preview(response.body));
        }
        if (response.status_code != 200)
            throw BoosteroidClientError("session/details failed: HTTP " + std::to_string(response.status_code) + ": " + Preview(response.body));

        if (response.body.empty())
        {
            last_empty_status = static_cast<int>(response.status_code);
            if (attempt < retries_on_empty_body)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                continue;
            }
            break;
        }

        JsonPtr root = Load(response.body);
        json_t* data = json_object_get(root.get(), "data");
        SessionInfo info;
        info.session_id = session_id;
        const std::string gw = GetGatewayAddress(data, "gw");
        if (!gw.empty())
            info.node_base_url = gw;
        const std::string qs = GetString(data, "queryString");
        if (!qs.empty())
            info.query_string = qs;
        info.status = "LI";
        return info;
    }
    throw BoosteroidClientError(
        "session/details: got " + std::to_string(retries_on_empty_body + 1) +
        " consecutive empty responses (HTTP " + std::to_string(last_empty_status) +
        ") - the session may have just been claimed by another device and hasn't settled yet. Try again in a moment.");
}

std::optional<SessionInfo> BoosteroidClient::DetailsIfReady(const std::string& session_id, const CookieJar& cookies) const
{
    const std::string url = std::string(kApiBaseUrl) + "/api/v1/streaming/session/details?sessionId=" + session_id;
    const HttpResponse response = http_client_.Post(url, kUserAgent, AuthenticatedHeaders(cookies), "");
    if (response.status_code == 406)
        return std::nullopt; // queued (or expired) - keep waiting.
    if (response.status_code != 200 || response.body.empty())
        return std::nullopt; // settling - keep waiting.

    JsonPtr root = TryLoad(response.body);
    if (!root)
        return std::nullopt;
    json_t* data = json_object_get(root.get(), "data");
    const std::string gw = GetGatewayAddress(data, "gw");
    const std::string qs = GetString(data, "queryString");

    if (!gw.empty())
    {
        SessionInfo info;
        info.session_id = session_id;
        info.node_base_url = gw;
        info.status = "LI";
        if (!qs.empty())
            info.query_string = qs;
        return info;
    }
    // No `gw` here, but the confirmation already named the host: that plus
    // queryString is everything the control socket needs.
    if (!preferred_gateway_.empty() && !qs.empty())
    {
        SessionInfo info;
        info.session_id = session_id;
        info.node_base_url = preferred_gateway_;
        info.status = "LI";
        info.query_string = qs;
        return info;
    }
    // NEVER guess a gateway from /v1/streaming/gateways (16 priority entries
    // on a typical account -> ~1-in-16 chance of picking the right node,
    // confirmed to break the control socket the other 15 times). Keep waiting.
    return std::nullopt;
}

SessionInfo BoosteroidClient::CreateSession(const SessionCreateRequest& request, const CookieJar& cookies) const
{
    const int app_id = std::atoi(request.game_id.c_str());

    // Resume a session that is genuinely still running for THIS game — proof
    // is session/details answering 200 with a gw, not last-session's status
    // (which can be stale). See BoosteroidClient.createSession's comment in
    // the Swift source for the full reasoning.
    if (auto last = FetchLastSession(cookies); last && last->app_id == app_id)
    {
        try
        {
            if (auto live = DetailsIfReady(last->session_id, cookies))
                return *live;
        }
        catch (...) { /* fall through to a fresh enqueue */ }
    }

    // Starting a DIFFERENT game (or nothing usable is running): release
    // whatever the account holds first, best-effort, then always enqueue
    // fresh (enqueue always creates a brand-new session and orphans any
    // other — CONFIRMED — which is the clean "switch to this Switch").
    if (auto existing = FetchLastSession(cookies))
        HangUpSession(existing->session_id, cookies);
    Dequeue(cookies);

    const std::string url = std::string(kApiBaseUrl) + "/api/v2/streaming/session/enqueue";
    JsonPtr body_json(json_object(), &json_decref);
    json_object_set_new(body_json.get(), "appId", json_integer(app_id));
    char* dump = json_dumps(body_json.get(), JSON_COMPACT);
    const std::string body = dump ? dump : "{}";
    if (dump)
        free(dump);

    const HttpResponse response = http_client_.Post(url, kUserAgent, AuthenticatedHeaders(cookies, true), body);
    if (response.status_code != 204)
        throw BoosteroidClientError("createSession/enqueue failed: HTTP " + std::to_string(response.status_code) + ": " + Preview(response.body));

    SessionInfo info;
    info.status = "EN";
    if (auto queued = FetchLastSession(cookies); queued && queued->app_id == app_id)
        info.session_id = queued->session_id;
    return info;
}

SessionInfo BoosteroidClient::CreateAndAwaitSession(
    const SessionCreateRequest& request,
    const CookieJar& cookies,
    std::function<bool(const SessionInfo&, int attempt)> on_poll,
    long long queued_poll_interval_ms,
    long long setup_poll_interval_ms,
    long long setup_timeout_seconds,
    const std::string& realtime_uid,
    const std::string& realtime_access_token) const
{
    const int app_id = std::atoi(request.game_id.c_str());
    SessionInfo current = CreateSession(request, cookies);
    if (on_poll && !on_poll(current, 0))
        throw BoosteroidClientError("createAndAwaitSession: cancelled");
    if (current.node_base_url)
        return current; // Resumed an already-live session.

    // The VM may already be ready with no queue at all.
    if (auto last = FetchLastSession(cookies); last && last->app_id == app_id)
    {
        if (auto ready = DetailsIfReady(last->session_id, cookies))
            return *ready;
    }

    // BUG FIXED 2026-07-31: this is the piece that was missing entirely.
    // preferred_session_id_ has always been checked below, but nothing ever
    // SET it — SetPreferredSessionId/StartStreamingSession had no caller
    // anywhere in the app, so any account that actually hit a real queue
    // ("EN" -> reserved "UN", needing the v2 confirmation) polled
    // last-session forever with no way to proceed. Wire the realtime
    // queue-position WebSocket up ourselves (CONFIRMED URL/auth in
    // BoosteroidATV's CLAUDE.md) and, on its queues/start push, do the
    // confirmation (StartStreamingSession) right here.
    std::unique_ptr<WebSocketClient> realtime_ws;
    bool realtime_confirmation_attempted = false;
    if (realtime_uid.empty() || realtime_access_token.empty())
    {
        // DIAGNOSTIC (added 2026-07-31): the first attempt at this fix had no
        // logging at all for the "we never even tried" case, which looked
        // identical in the log to "connected fine, no push arrived yet" —
        // undiagnosable from a runtime.log alone. Log which one it was.
        LogRuntimeEvent("session", "realtime_queue_skipped",
                         std::string("uid_empty=") + (realtime_uid.empty() ? "true" : "false") +
                         " token_empty=" + (realtime_access_token.empty() ? "true" : "false"));
    }
    if (!realtime_uid.empty() && !realtime_access_token.empty())
    {
        realtime_ws = std::make_unique<WebSocketClient>(
            "wss://cloud.boosteroid.com/ws?uid=" + realtime_uid + "&token=" + realtime_access_token);
        realtime_ws->set_on_message([this, &realtime_confirmation_attempted, app_id, &cookies](const std::string& text) {
            // DIAGNOSTIC: log every inbound message (truncated) regardless of
            // whether it matches queues/start, so a runtime.log capture can
            // show the REAL message shape if the queues/start match below
            // ever misfires (field names/nesting here are inferred from
            // BoosteroidATV's CLAUDE.md, not captured directly for this app).
            LogRuntimeEvent("session", "realtime_message", Preview(text));

            if (realtime_confirmation_attempted)
                return; // One-shot: StartStreamingSession must never be retried (429 lockout risk).

            JsonPtr root = TryLoad(text);
            if (!root)
                return;
            json_t* msg = root.get();
            const std::string type = GetString(msg, "type");
            const std::string action = GetString(msg, "action");
            if (type != "queues" || action != "start")
                return;

            // Fields have been observed described as {appId, token} on the
            // queues/start push itself; be lenient and also check a nested
            // "value" object in case the exact nesting differs from a plain
            // top-level pair (unconfirmed at that level of detail — see
            // boosteroid_client.hpp's CreateAndAwaitSession doc comment).
            json_t* fields = json_object_get(msg, "value");
            if (!fields || !json_is_object(fields))
                fields = msg;
            const int pushed_app_id = GetInteger(fields, "appId", -1);
            const std::string token = GetString(fields, "token");
            if (pushed_app_id != app_id || token.empty())
                return;

            realtime_confirmation_attempted = true;
            LogRuntimeEvent("session", "realtime_queue_start_received",
                             "appId=" + std::to_string(app_id));
            try
            {
                const StartResult start = StartStreamingSession(app_id, token, cookies);
                JsonPtr start_root = TryLoad(start.body);
                json_t* data = start_root ? json_object_get(start_root.get(), "data") : nullptr;
                const std::string session_id = data ? GetString(data, "sessionId") : "";
                std::string gateway;
                if (data)
                {
                    json_t* gateways = json_object_get(data, "gateways");
                    if (json_is_array(gateways) && json_array_size(gateways) > 0)
                        gateway = GetString(json_array_get(gateways, 0), "address");
                }
                if (start.status == 201 && !session_id.empty())
                {
                    SetPreferredSessionId(session_id);
                    if (!gateway.empty())
                        SetPreferredGateway(gateway);
                    LogRuntimeEvent("session", "realtime_queue_start_confirmed",
                                     "sessionId=" + session_id + " gateway=" + gateway);
                }
                else
                {
                    LogRuntimeEvent("session", "realtime_queue_start_confirm_failed",
                                     "status=" + std::to_string(start.status) + " body=" + Preview(start.body));
                }
            }
            catch (const std::exception& ex)
            {
                // Don't rethrow out of a WebSocket message callback — let the
                // existing REST-polling loop below run its course (and
                // eventually time out) instead of crashing the whole launch.
                LogRuntimeEvent("session", "realtime_queue_start_confirm_error", ex.what());
            }
        });
        if (!realtime_ws->connect())
        {
            LogRuntimeEvent("session", "realtime_queue_connect_failed", realtime_ws->get_last_error());
            realtime_ws.reset();
        }
        else
        {
            LogRuntimeEvent("session", "realtime_queue_connected", "uid=" + realtime_uid);
        }
    }

    int attempt = 0;
    bool have_setup_deadline = false;
    std::chrono::steady_clock::time_point setup_deadline;
    long long interval_ms = queued_poll_interval_ms;
    bool reacted_to_confirmation = false;

    // Safety net: even with the realtime socket wired up above, don't poll
    // "EN" (queued) forever if it never delivers a matching queues/start (an
    // unconfirmed message shape, a disconnect, or this account's queue
    // genuinely never draining) — surface an actionable error instead of the
    // launch dialog hanging silently, which is what real hardware testing
    // 2026-07-31 hit before this fix existed.
    constexpr long long kQueuedTimeoutSeconds = 600;
    const auto queued_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kQueuedTimeoutSeconds);

    for (;;)
    {
        // Sleep in slices so a preferred-session confirmation (delivered by
        // the realtime socket above, or set directly by the caller via
        // SetPreferredSessionId) is noticed promptly instead of waiting out
        // a whole 60s slow-poll interval, and so the realtime socket itself
        // gets polled regularly.
        constexpr long long kSliceMs = 2000;
        long long slept_ms = 0;
        while (slept_ms < interval_ms)
        {
            const long long chunk = std::min<long long>(kSliceMs, interval_ms - slept_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
            if (realtime_ws)
                realtime_ws->poll();
            slept_ms += chunk;
            if (!preferred_session_id_.empty() && !reacted_to_confirmation)
                break;
        }
        attempt++;

        if (!preferred_session_id_.empty())
        {
            reacted_to_confirmation = true;
            SessionInfo confirmed;
            confirmed.session_id = preferred_session_id_;
            confirmed.status = "confirmed";
            if (on_poll && !on_poll(confirmed, attempt))
                throw BoosteroidClientError("createAndAwaitSession: cancelled");
            if (auto ready = DetailsIfReady(preferred_session_id_, cookies))
                return *ready;

            interval_ms = setup_poll_interval_ms;
            if (!have_setup_deadline)
            {
                setup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(setup_timeout_seconds);
                have_setup_deadline = true;
            }
            if (std::chrono::steady_clock::now() > setup_deadline)
                throw BoosteroidClientError("The machine was confirmed but never became ready in time. Try again.");
            continue;
        }

        auto last = FetchLastSession(cookies);
        // DIAGNOSTIC: the http-level log only shows status codes/byte counts
        // for last-session, not the actual EN/UN/LI status string it
        // reports — log it directly so a runtime.log capture shows queue
        // progress without guessing from response sizes.
        LogRuntimeEvent("session", "last_session_poll",
                         "attempt=" + std::to_string(attempt) +
                         " found=" + (last ? "true" : "false") +
                         (last ? " appId=" + std::to_string(last->app_id) + " status=" + last->status : "") +
                         " awaited_appId=" + std::to_string(app_id));
        if (last && last->app_id == app_id)
        {
            current.session_id = last->session_id;
            current.status = last->status;
            if (on_poll && !on_poll(current, attempt))
                throw BoosteroidClientError("createAndAwaitSession: cancelled");

            if (last->status != "EN")
            {
                if (auto ready = DetailsIfReady(last->session_id, cookies))
                    return *ready;
            }

            if (last->status == "EN")
            {
                have_setup_deadline = false; // Still queued - no per-setup deadline.
                interval_ms = queued_poll_interval_ms;
                if (std::chrono::steady_clock::now() > queued_deadline)
                {
                    throw BoosteroidClientError(
                        "Still queued after " + std::to_string(kQueuedTimeoutSeconds / 60) +
                        " minutes with no confirmation from Boosteroid's queue socket. "
                        "Try starting the session from a browser or the official app first, "
                        "then relaunch here to take it over.");
                }
            }
            else
            {
                interval_ms = setup_poll_interval_ms;
                if (!have_setup_deadline)
                {
                    setup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(setup_timeout_seconds);
                    have_setup_deadline = true;
                }
                if (std::chrono::steady_clock::now() > setup_deadline)
                    throw BoosteroidClientError("The queue cleared but the machine didn't finish setting up in time. Try again.");
            }
        }
        else
        {
            SessionInfo waiting;
            waiting.session_id = current.session_id;
            waiting.status = last ? "another game is queued" : "no session yet";
            if (on_poll && !on_poll(waiting, attempt))
                throw BoosteroidClientError("createAndAwaitSession: cancelled");
        }

        // NEVER call StartStreamingSession from this loop on a timer - it is
        // a one-shot confirmation and was CONFIRMED to earn an HTTP 429
        // lockout when retried. It is only ever called once, from the
        // realtime socket's on_message handler above, in reaction to its
        // queues/start push.
    }
}

// MARK: - Local session persistence

namespace {
std::string SessionPath() { return AppHomePath() + "/boosteroid_session.json"; }
std::string ReadTextFile(const std::string& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
        return "";
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
} // namespace

bool BoosteroidClient::LoadSavedSession(AuthSession& session) const {
    const std::string body = ReadTextFile(SessionPath());
    if (body.empty())
        return false;
    JsonPtr root = TryLoad(body);
    if (!root || !json_is_object(root.get()))
        return false;

    json_t* user = json_object_get(root.get(), "user");
    session.user.user_id = GetString(user, "user_id");
    session.user.display_name = GetString(user, "display_name");
    session.user.email = GetString(user, "email");
    session.user.avatar_url = GetString(user, "avatar_url");
    session.user.membership_tier = "unknown";

    json_t* tokens = json_object_get(root.get(), "tokens");
    session.tokens.access_token = GetString(tokens, "access_token");
    session.tokens.refresh_token = GetString(tokens, "refresh_token");
    session.tokens.expires_at_ms = static_cast<std::int64_t>(GetInteger(tokens, "expires_at_ms", 0));
    session.tokens.session_cookies.clear();
    json_t* cookies = tokens ? json_object_get(tokens, "session_cookies") : nullptr;
    if (json_is_array(cookies)) {
        size_t index = 0;
        json_t* item = nullptr;
        json_array_foreach(cookies, index, item) {
            session.tokens.session_cookies.emplace_back(GetString(item, "name"), GetString(item, "value"));
        }
    }
    session.reauthentication_required = false;
    return !session.user.user_id.empty() && session.user.user_id != "unknown";
}

void BoosteroidClient::SaveSession(const AuthSession& session) const {
    PrepareAppStorage();

    JsonPtr user(json_object(), &json_decref);
    json_object_set_new(user.get(), "user_id", json_string(session.user.user_id.c_str()));
    json_object_set_new(user.get(), "display_name", json_string(session.user.display_name.c_str()));
    json_object_set_new(user.get(), "email", json_string(session.user.email.c_str()));
    json_object_set_new(user.get(), "avatar_url", json_string(session.user.avatar_url.c_str()));

    JsonPtr cookies(json_array(), &json_decref);
    for (const auto& [name, value] : session.tokens.session_cookies) {
        JsonPtr entry(json_object(), &json_decref);
        json_object_set_new(entry.get(), "name", json_string(name.c_str()));
        json_object_set_new(entry.get(), "value", json_string(value.c_str()));
        json_array_append_new(cookies.get(), json_incref(entry.get()));
    }

    JsonPtr tokens(json_object(), &json_decref);
    json_object_set_new(tokens.get(), "access_token", json_string(session.tokens.access_token.c_str()));
    json_object_set_new(tokens.get(), "refresh_token", json_string(session.tokens.refresh_token.c_str()));
    json_object_set_new(tokens.get(), "expires_at_ms", json_integer(session.tokens.expires_at_ms));
    json_object_set_new(tokens.get(), "session_cookies", json_incref(cookies.get()));

    JsonPtr root(json_object(), &json_decref);
    json_object_set_new(root.get(), "user", json_incref(user.get()));
    json_object_set_new(root.get(), "tokens", json_incref(tokens.get()));

    char* dump = json_dumps(root.get(), JSON_INDENT(2));
    if (!dump)
        return;
    const std::string path = SessionPath();
    const std::string temporary_path = path + ".tmp";
    {
        std::ofstream stream(temporary_path, std::ios::binary | std::ios::trunc);
        if (stream.is_open()) {
            stream << dump;
        }
    }
    free(dump);
    storage::ReplaceWithTemporaryFile(temporary_path, path);
}

void BoosteroidClient::ClearSavedSession() const {
    std::remove(SessionPath().c_str());
    std::remove((SessionPath() + ".bak").c_str());
}

} // namespace opennow
