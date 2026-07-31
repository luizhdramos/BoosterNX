#pragma once

#include "boosteroid_client.hpp" // CookieJar
#include "http_client.hpp"
#include "models.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// MARK: - Boosteroid WebRTC Signaling Client (REST, webrtc-streamer-shaped)
//
// Ported from BoosteroidATV's SignalingClient.swift, whose header comment has
// the full confirmation trail. CONFIRMED 2026-07-22 by capturing real traffic
// while starting/ending an actual eFootball stream on cloud.boosteroid.com:
// this REST API matches the shape of the open-source "webrtc-streamer"
// project (github.com/mpromonet/webrtc-streamer) — getIceServers, getParams,
// call, addIceCandidate, getIceCandidate, all against the per-session
// gateway host from SessionInfo::node_base_url. UNLIKE GFN's WebSocket
// signaling (which SwitchNOW's old signaling_client.cpp implemented), there
// is no persistent socket here — each call is an independent HTTP request.
//
// Client-is-offerer (CONFIRMED, unlike GFN where the server offers): create
// the local SDP offer, POST it to `call`, receive the answer in the
// response. Trickle ICE both ways: local candidates POST to
// addIceCandidate, remote candidates are polled via getIceCandidate.
namespace opennow
{

struct StreamParams
{
    std::string codec; // CONFIRMED "H264" in every capture so far.
    int version = 0;
};

class BoosteroidSignalingError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class BoosteroidSignalingClient
{
  public:
    BoosteroidSignalingClient(std::string node_base_url, std::string session_id, CookieJar cookies);

    // CONFIRMED verbatim response: {"iceServers":[{"credential":"...",
    // "urls":["turn:HOST:3478?transport=udp"],"username":"<unixSeconds>:
    // boosteroid"}],"iceTransportPolicy":"all"}. Retries while the node is
    // still booting (see GetUntilReady).
    std::vector<IceServerInfo> FetchIceServers() const;

    // CONFIRMED verbatim response: {"codec":"H264","version":1}.
    StreamParams FetchParams() const;

    // POST call?peerid=...&sessionId=... with the local offer SDP, returns
    // the remote answer SDP. TODO(protocol): exact request/response body
    // assumed from the upstream webrtc-streamer OSS convention
    // ({"sdp":...,"type":"offer"} in / {"sdp":...} out) — not confirmed
    // byte-for-byte (see the Swift source's header comment).
    std::string SendOffer(const std::string& sdp) const;

    // POST addIceCandidate?peerid=...&sessionId=... — fire-and-forget, one
    // call per locally-trickled ICE candidate.
    void SendIceCandidate(const std::string& candidate, const std::string& sdp_mid, int sdp_mline_index) const;

    struct RemoteCandidate
    {
        std::string candidate;
        std::string sdp_mid;
        int sdp_mline_index = 0;
    };
    // GET getIceCandidate?peerid=...&sessionId=... — REST polling (not
    // push). Returns whatever the node currently has queued; caller is
    // expected to call this repeatedly (e.g. every second) from a background
    // thread until the peer connection is up. TODO(protocol): polling
    // interval and whether it should stop after connect are both unconfirmed
    // guesses.
    std::vector<RemoteCandidate> PollRemoteIceCandidates() const;

  private:
    std::string node_base_url_;
    std::string session_id_;
    CookieJar cookies_;
    std::string peer_id_;
    HttpClient http_client_;

    std::string BuildUrl(const std::string& path, bool include_peer_id) const;
    std::vector<std::string> AuthHeaders(bool json_body = false) const;
    // GETs a signaling endpoint, retrying while the assigned machine exists
    // but its streaming service isn't up yet (CONFIRMED real failure body:
    // 502 {"error":"Bad Gateway","message":"Target service unavailable"} —
    // session/details hands out the gateway as soon as status is "LI", which
    // is EARLIER than the VM being able to negotiate).
    std::string GetUntilReady(const std::string& path, double ready_timeout_seconds = 90) const;
};

} // namespace opennow
