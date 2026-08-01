#include "boosteroid_signaling_client.hpp"
#include "json_utils.hpp"
#include "runtime_journal.hpp"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <sstream>
#include <thread>

using namespace opennow::json;

namespace opennow
{
namespace
{
constexpr const char* kUserAgent =
    "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36";
constexpr const char* kApiBaseUrl = "https://cloud.boosteroid.com";

std::string MakePeerId()
{
    // CONFIRMED: the real client uses a plain JS Math.random() value as a
    // string, e.g. "0.9150882553499954" — not a UUID.
    std::ostringstream out;
    out << (static_cast<double>(rand()) / (static_cast<double>(RAND_MAX) + 1.0));
    return out.str();
}

std::string Preview(const std::string& body, size_t max_chars = 200)
{
    return body.size() > max_chars ? body.substr(0, max_chars) + "..." : body;
}

bool LooksLikeBootingUp(int status, const std::string& body)
{
    if (status >= 500 || body.empty())
        return true;
    std::string lower = body;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower.find("unavailable") != std::string::npos;
}

} // namespace

BoosteroidSignalingClient::BoosteroidSignalingClient(std::string node_base_url, std::string session_id, CookieJar cookies)
    : node_base_url_(std::move(node_base_url)), session_id_(std::move(session_id)), cookies_(std::move(cookies)), peer_id_(MakePeerId())
{
}

std::vector<std::string> BoosteroidSignalingClient::AuthHeaders(bool json_body) const
{
    std::vector<std::string> headers = {
        "Cookie: " + BuildCookieHeader(cookies_),
        std::string("Origin: ") + kApiBaseUrl,
        std::string("Referer: ") + kApiBaseUrl + "/dashboard",
        "Accept: application/json",
    };
    if (json_body)
        headers.push_back("Content-Type: application/json");
    return headers;
}

std::string BoosteroidSignalingClient::BuildUrl(const std::string& path, bool include_peer_id) const
{
    std::ostringstream url;
    url << node_base_url_ << "/webrtc/api/" << path << "?sessionId=" << session_id_;
    if (include_peer_id)
        url << "&peerid=" << peer_id_;
    return url.str();
}

std::string BoosteroidSignalingClient::GetUntilReady(const std::string& path, double ready_timeout_seconds) const
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<long long>(ready_timeout_seconds * 1000));
    int last_status = 0;
    std::string last_body;
    // getIceServers/getParams never need a peerid (CONFIRMED: only call/
    // addIceCandidate/getIceCandidate take one).
    const std::string url = BuildUrl(path, false);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const HttpResponse response = http_client_.Get(url, kUserAgent, AuthHeaders());
        last_status = static_cast<int>(response.status_code);
        last_body = response.body;
        if (!LooksLikeBootingUp(last_status, last_body))
            return last_body;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    throw BoosteroidSignalingError(
        path + " never became available within " + std::to_string(static_cast<int>(ready_timeout_seconds)) +
        "s (last HTTP " + std::to_string(last_status) + ", body: " + Preview(last_body) +
        "). The assigned machine's streaming service never came up.");
}

std::vector<IceServerInfo> BoosteroidSignalingClient::FetchIceServers() const
{
    const std::string body = GetUntilReady("getIceServers");
    JsonPtr root = TryLoad(body);
    json_t* servers = root ? json_object_get(root.get(), "iceServers") : nullptr;
    std::vector<IceServerInfo> result;
    if (json_is_array(servers))
    {
        size_t index = 0;
        json_t* item = nullptr;
        json_array_foreach(servers, index, item)
        {
            const std::string username = GetString(item, "username");
            const std::string credential = GetString(item, "credential");
            json_t* urls = json_object_get(item, "urls");
            if (json_is_array(urls))
            {
                size_t url_index = 0;
                json_t* url_item = nullptr;
                json_array_foreach(urls, url_index, url_item)
                {
                    if (json_is_string(url_item))
                        result.push_back({json_string_value(url_item), username, credential});
                }
            }
            else if (json_is_string(json_object_get(item, "urls")))
            {
                result.push_back({GetString(item, "urls"), username, credential});
            }
        }
    }
    if (result.empty())
        throw BoosteroidSignalingError("getIceServers returned no ICE servers (body: " + Preview(body) + ").");
    return result;
}

StreamParams BoosteroidSignalingClient::FetchParams() const
{
    const std::string body = GetUntilReady("getParams");
    JsonPtr root = TryLoad(body);
    if (!root)
        throw BoosteroidSignalingError("getParams returned unusable params (body: " + Preview(body) + ").");
    StreamParams params;
    params.codec = GetString(root.get(), "codec");
    params.version = GetInteger(root.get(), "version");
    return params;
}

std::string BoosteroidSignalingClient::SendOffer(const std::string& sdp) const
{
    JsonPtr body_json(json_object(), &json_decref);
    json_object_set_new(body_json.get(), "sdp", json_string(sdp.c_str()));
    json_object_set_new(body_json.get(), "type", json_string("offer"));
    char* dump = json_dumps(body_json.get(), JSON_COMPACT);
    const std::string body = dump ? dump : "{}";
    if (dump)
        free(dump);

    const HttpResponse response = http_client_.Post(BuildUrl("call", true), kUserAgent, AuthHeaders(true), body);
    if (response.status_code != 200)
        throw BoosteroidSignalingError("call failed: " + Preview(response.body));

    JsonPtr root = TryLoad(response.body);
    const std::string answer_sdp = root ? GetString(root.get(), "sdp") : "";
    if (answer_sdp.empty())
        throw BoosteroidSignalingError("call: no sdp in response: " + Preview(response.body));
    return answer_sdp;
}

void BoosteroidSignalingClient::SendIceCandidate(const std::string& candidate, const std::string& sdp_mid, int sdp_mline_index) const
{
    JsonPtr body_json(json_object(), &json_decref);
    json_object_set_new(body_json.get(), "candidate", json_string(candidate.c_str()));
    if (!sdp_mid.empty())
        json_object_set_new(body_json.get(), "sdpMid", json_string(sdp_mid.c_str()));
    json_object_set_new(body_json.get(), "sdpMLineIndex", json_integer(sdp_mline_index));
    char* dump = json_dumps(body_json.get(), JSON_COMPACT);
    const std::string body = dump ? dump : "{}";
    if (dump)
        free(dump);
    // Fire-and-forget as far as the session's fate goes (matching the Swift
    // client — a failure here just means one fewer candidate reaches the
    // server), but NOT silent: real hardware testing 2026-07-31 saw every
    // addIceCandidate come back HTTP 500 while the stream then received zero
    // packets, and the response body was being discarded, so there was
    // nothing to diagnose from. The exact request body shape here is
    // UNCONFIRMED (assumed from the upstream webrtc-streamer OSS convention,
    // see this class's header) — logging what we sent alongside what the
    // server said back is what will confirm or refute it.
    try
    {
        const HttpResponse response =
            http_client_.Post(BuildUrl("addIceCandidate", true), kUserAgent, AuthHeaders(true), body);
        if (response.status_code < 200 || response.status_code >= 300)
        {
            LogRuntimeEvent("webrtc", "add_ice_candidate_failed",
                             "status=" + std::to_string(response.status_code) +
                             " response=" + Preview(response.body, 120) +
                             " sent=" + Preview(body, 200));
        }
        else
        {
            LogRuntimeEvent("webrtc", "add_ice_candidate_ok", Preview(candidate, 120));
        }
    }
    catch (const std::exception& ex)
    {
        LogRuntimeEvent("webrtc", "add_ice_candidate_error", ex.what());
    }
}

std::vector<BoosteroidSignalingClient::RemoteCandidate> BoosteroidSignalingClient::PollRemoteIceCandidates() const
{
    std::vector<RemoteCandidate> result;
    HttpResponse response;
    try
    {
        response = http_client_.Get(BuildUrl("getIceCandidate", true), kUserAgent, AuthHeaders());
    }
    catch (...)
    {
        return result;
    }
    if (response.status_code != 200)
        return result;

    JsonPtr root = TryLoad(response.body);
    if (!root || !json_is_array(root.get()))
    {
        // Logged once per distinct body rather than every poll (this runs on
        // a ~1s timer): a shape mismatch here would silently drop EVERY
        // remote candidate, which looks identical to "the server sent none"
        // and would perfectly explain a peer that never connects. The
        // response shape is UNCONFIRMED (OSS convention) — see the header.
        static std::string last_unparsed;
        if (response.body != last_unparsed)
        {
            last_unparsed = response.body;
            LogRuntimeEvent("webrtc", "remote_candidates_unparsed", Preview(response.body, 200));
        }
        return result;
    }

    size_t index = 0;
    json_t* item = nullptr;
    json_array_foreach(root.get(), index, item)
    {
        const std::string candidate = GetString(item, "candidate");
        if (candidate.empty())
            continue;
        RemoteCandidate remote;
        remote.candidate = candidate;
        remote.sdp_mid = GetString(item, "sdpMid");
        remote.sdp_mline_index = GetInteger(item, "sdpMLineIndex");
        result.push_back(std::move(remote));
    }

    // Same rationale: log only when the set actually changes, so a 1s poll
    // loop doesn't flood runtime.log across a whole session.
    static std::string last_summary;
    std::string summary = "count=" + std::to_string(result.size());
    for (const auto& remote : result)
    {
        const size_t typ = remote.candidate.find(" typ ");
        summary += " [" + (typ == std::string::npos
                               ? std::string("?")
                               : remote.candidate.substr(typ + 5, remote.candidate.find(' ', typ + 5) - typ - 5)) +
                   "]";
    }
    if (summary != last_summary)
    {
        last_summary = summary;
        LogRuntimeEvent("webrtc", "remote_candidates", summary);
    }
    return result;
}

} // namespace opennow
