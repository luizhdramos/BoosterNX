#include "webrtc_session.hpp"
#include "network_loop_policy.hpp"
#include "internal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>

#ifdef __SWITCH__
#include <sys/stat.h>
#endif

using namespace opennow::webrtc::internal;
using opennow::BoosteroidControlEvent;
using opennow::BoosteroidSignalingClient;
using opennow::IceServerInfo;

namespace
{
constexpr const char* kNvdecActiveMarker = "sdmc:/switch/BoosterNX/nvdec_active.marker";

bool ConsumeNvdecCrashMarker()
{
    std::ifstream marker(kNvdecActiveMarker, std::ios::binary);
    const bool exists = marker.good();
    marker.close();
    if (exists)
        std::remove(kNvdecActiveMarker);
    return exists;
}

bool CreateNvdecCrashMarker()
{
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/BoosterNX", 0777);
#endif
    std::ofstream marker(kNvdecActiveMarker, std::ios::binary | std::ios::trunc);
    if (!marker.is_open())
        return false;
    marker << "NVDEC stream active; remove after clean shutdown\n";
    marker.flush();
    return marker.good();
}

} // namespace

WebRtcSession::WebRtcSession(
    const std::string& node_base_url,
    const std::string& session_id,
    const std::string& query_string,
    const opennow::CookieJar& cookies,
    const opennow::StreamSettings& settings)
    : node_base_url_(node_base_url),
      session_id_(session_id),
      query_string_(query_string),
      cookies_(cookies),
      settings_(settings) {
    peer_init();
    renderer_ = std::make_unique<DKVideoRenderer>();
    audio_ = std::make_unique<AudioPipeline>();
    audio_->configure(1200, 40);

    const bool previous_nvdec_crash = settings_.video_backend == "Auto" && ConsumeNvdecCrashMarker();
    const bool force_software = settings_.video_backend == "Software" || previous_nvdec_crash;
    decoder_ = std::make_unique<FFmpegVideoDecoder>();
    decoder_setup_result_ = decoder_->setup(
        VIDEO_FORMAT_H264, settings_.width, settings_.height, settings_.fps, nullptr,
        force_software ? VIDEO_DECODER_FORCE_SOFTWARE : VIDEO_DECODER_PREFER_HARDWARE);

    if (decoder_setup_result_ != 0 && !force_software) {
        decoder_->cleanup();
        decoder_ = std::make_unique<FFmpegVideoDecoder>();
        decoder_setup_result_ = decoder_->setup(
            VIDEO_FORMAT_H264, settings_.width, settings_.height, settings_.fps, nullptr,
            VIDEO_DECODER_FORCE_SOFTWARE);
        decoder_fallback_used_ = decoder_setup_result_ == 0;
    }

    if (decoder_setup_result_ == 0 && decoder_->uses_hardware_frames())
        video_backend_name_ = "NVDEC-NVTEGRA/Deko3D-zero-copy";
    else if (decoder_setup_result_ == 0)
        video_backend_name_ = decoder_fallback_used_
            ? "FFmpeg-SW-3T/Deko3D-upload(auto-fallback)"
            : "FFmpeg-SW-3T/Deko3D-upload";
    else
        video_backend_name_ = "decoder-setup-failed";

    if (decoder_setup_result_ == 0 && decoder_->uses_hardware_frames())
        CreateNvdecCrashMarker();
}

WebRtcSession::~WebRtcSession() {
    stop();
    peer_deinit();
}

void WebRtcSession::start() {
    stop_requested_.store(false, std::memory_order_release);
    session_started_at_ = std::chrono::steady_clock::now();
    ResetStreamTraceLog();
    AppendStreamLog("SESSION start node=" + node_base_url_ + " sessionId=" + session_id_ +
                    " " + std::to_string(settings_.width) + "x" + std::to_string(settings_.height) +
                    "@" + std::to_string(settings_.fps));
    AppendStreamLog("VIDEO backend=" + video_backend_name_ +
                    " requested=" + settings_.video_backend +
                    " fallback=" + std::to_string(decoder_fallback_used_ ? 1 : 0) +
                    " decoderSetup=" + std::to_string(decoder_setup_result_));

    if (audio_)
        audio_->start();
    start_decoder_worker();
    start_network_worker();

    // CONFIRMED (StreamController.swift, BoosteroidControlChannel.swift): the
    // control socket is the PRIMARY connection. Opening it claims the session
    // for this device; WebRTC signaling only starts after it signals
    // WebrtcEngineReady (fresh session) or SessionActive (take-over/switch).
    current_state_ = "Opening control channel";
    AppendStreamLog("CONTROL connecting");
    const int width = settings_.width, height = settings_.height;
    const int bitrate_bps = opennow::TargetBitrateBps(settings_, width, height);
    const bool connected = control_channel_.Connect(
        node_base_url_, query_string_, width, height, settings_.fps, bitrate_bps,
        [this](const BoosteroidControlEvent& event) { handle_control_event(event); });

    if (!connected) {
        current_state_ = "Control channel connect failed";
        AppendStreamLog("CONTROL error connect_failed");
        return;
    }
    control_channel_alive_.store(true, std::memory_order_relaxed);
    current_state_ = "Control channel open, waiting for server to start video";
    AppendStreamLog("CONTROL open");
}

void WebRtcSession::handle_control_event(const BoosteroidControlEvent& event) {
    using Type = BoosteroidControlEvent::Type;
    switch (event.type) {
        case Type::WebrtcEngineReady:
            AppendStreamLog("CONTROL settings/webrtc (start engine)");
            if (!webrtc_started_.exchange(true))
                start_webrtc_media();
            break;
        case Type::SessionActive:
            AppendStreamLog("CONTROL stream/* burst (session active)");
            if (!webrtc_started_.exchange(true))
                start_webrtc_media();
            break;
        case Type::ControllerAck: {
            for (auto& controller : controllers_) {
                if (controller.connected && !controller.acked && controller.pending_name == event.controller_name) {
                    controller.acked = true;
                    char* end = nullptr;
                    const long parsed = std::strtol(event.controller_id.c_str(), &end, 10);
                    if (end && *end == '\0')
                        controller.server_id = static_cast<int>(parsed);
                    break;
                }
            }
            AppendInputLog("controller ack name=" + event.controller_name + " id=" + event.controller_id);
            break;
        }
        case Type::Closed:
            control_channel_alive_.store(false, std::memory_order_relaxed);
            AppendStreamLog("CONTROL closed");
            break;
        case Type::Failed:
            control_channel_alive_.store(false, std::memory_order_relaxed);
            AppendStreamLog("CONTROL failed: " + event.error_message);
            if (!webrtc_started_.load())
                current_state_ = "Control channel failed before streaming could start: " + event.error_message;
            break;
        case Type::Raw:
            AppendStreamLog("CONTROL raw " + event.raw_type + "/" + event.raw_action);
            break;
        default:
            break;
    }
}

// CONFIRMED chain (StreamController.swift): getIceServers -> getParams ->
// create local offer -> POST call -> set remote answer -> flush buffered
// ICE -> start polling remote ICE. Client is the offerer, unlike GFN.
void WebRtcSession::start_webrtc_media() {
    current_state_ = "Fetching ICE servers";
    AppendStreamLog("WEBRTC start");
    try {
        signaling_ = std::make_unique<BoosteroidSignalingClient>(node_base_url_, session_id_, cookies_);
        const std::vector<IceServerInfo> ice_servers = signaling_->FetchIceServers();
        const auto params = signaling_->FetchParams();
        AppendStreamLog("WEBRTC params codec=" + params.codec + " version=" + std::to_string(params.version));

        setup_peer_connection(ice_servers);
        if (!pc_) {
            current_state_ = "PeerConnection setup failed";
            AppendStreamLog("WEBRTC error peer_connection_setup_failed");
            return;
        }

        const char* offer_sdp = peer_connection_create_offer(pc_);
        if (!offer_sdp) {
            current_state_ = "Failed to create local SDP offer";
            AppendStreamLog("WEBRTC error create_offer_failed");
            return;
        }
        AppendTraceBlock("LOCAL OFFER", offer_sdp);
        // No separate "set local description" call: peer_connection_create_offer()
        // above already builds pc->sdp and sets pc->b_local_description_created
        // internally (see extern/libpeer/src/peer_connection.c). The header
        // declares peer_connection_set_local_description() but this vendored
        // libpeer copy never implements it (only set_remote_description exists,
        // for SDP received from the server) — calling it was a link error.

        current_state_ = "Sending offer";
        const std::string answer_sdp = signaling_->SendOffer(offer_sdp);
        AppendTraceBlock("REMOTE ANSWER", answer_sdp);
        peer_connection_set_remote_description(pc_, answer_sdp.c_str(), SDP_TYPE_ANSWER);

        // Answer is set - safe to flush any ICE candidates gathered before now
        // (matches webrtcstreamer.js's "earlyCandidates" buffering; sending
        // before `call` loses them since the server hasn't registered our
        // peer yet).
        {
            std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
            ice_can_send_ = true;
            for (const auto& candidate : pending_local_ice_)
                signaling_->SendIceCandidate(candidate.sdp, candidate.sdp_mid, candidate.sdp_mline_index);
            pending_local_ice_.clear();
        }

        current_state_ = "Offer accepted, waiting for video";
        start_ice_poll_worker();
    } catch (const std::exception& ex) {
        current_state_ = std::string("Video setup failed: ") + ex.what();
        AppendStreamLog(std::string("WEBRTC error ") + ex.what());
    }
}

void WebRtcSession::start_ice_poll_worker() {
    if (ice_poll_running_.exchange(true))
        return;
    ice_poll_thread_ = std::thread([this] {
        // TODO(protocol): 1s polling interval is a guess (the capture this
        // was ported from only showed one getIceCandidate call) — see
        // BoosteroidSignalingClient's header comment.
        while (ice_poll_running_.load(std::memory_order_acquire) && !stop_requested_.load(std::memory_order_acquire)) {
            if (signaling_) {
                for (const auto& candidate : signaling_->PollRemoteIceCandidates()) {
                    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
                    if (pc_) {
                        std::string mutable_candidate = candidate.candidate;
                        peer_connection_add_ice_candidate(pc_, mutable_candidate.data());
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

void WebRtcSession::stop_ice_poll_worker() {
    ice_poll_running_.store(false, std::memory_order_release);
    if (ice_poll_thread_.joinable())
        ice_poll_thread_.join();
}

void WebRtcSession::setup_peer_connection(const std::vector<IceServerInfo>& ice_servers_in) {
    std::vector<IceServerInfo> ice_servers = ice_servers_in;
    if (ice_servers.empty()) {
        // Generic public STUN fallback only - never a GFN/NVIDIA-specific
        // host, and Boosteroid's own TURN servers (from getIceServers) are
        // what actually relays media in practice.
        ice_servers.push_back({"stun:stun.l.google.com:19302", "", ""});
        ice_servers.push_back({"stun:stun1.l.google.com:19302", "", ""});
    }

    PeerConfiguration config = {};
    config.video_codec = CODEC_H264;
    config.audio_codec = CODEC_OPUS;
    // CONFIRMED (StreamController.swift): Boosteroid's own webrtcstreamer.js
    // always includes a "ClientDataChannel" (m=application) in its offer and
    // the server appears to gate video on its presence, even though no input
    // or other traffic actually rides it. See internal.hpp's header comment
    // for why this port registers no ondatachannel callback at all.
    config.datachannel = DATA_CHANNEL_STRING;
    config.onvideopacket = on_video_packet_cb;
    config.onaudiopacket = on_audio_packet_cb;
    config.onrtpsenderreport = on_rtp_sender_report_cb;
    config.user_data = this;

    const size_t ice_count = std::min<size_t>(ice_servers.size(), 5);
    for (size_t i = 0; i < ice_count; ++i) {
        config.ice_servers[i].urls = ice_servers[i].url.c_str();
        config.ice_servers[i].username = ice_servers[i].username.empty() ? nullptr : ice_servers[i].username.c_str();
        config.ice_servers[i].credential = ice_servers[i].credential.empty() ? nullptr : ice_servers[i].credential.c_str();
    }
    // NOTE: config.ice_servers[i].urls/username/credential point into
    // `ice_servers` (a local vector) — safe here because peer_connection_create
    // copies what it needs synchronously before this function returns.

    pc_ = peer_connection_create(&config);
    if (!pc_)
        return;

    peer_connection_onicecandidate(pc_, on_ice_candidate_cb);
    peer_connection_oniceconnectionstatechange(pc_, on_peer_state_change_cb);
}

void WebRtcSession::start_network_worker() {
    if (network_running_.exchange(true))
        return;
    network_thread_ = std::thread(&WebRtcSession::network_loop, this);
    AppendStreamLog("TRANSPORT worker_started");
}

void WebRtcSession::network_loop() {
    while (network_running_.load(std::memory_order_acquire)) {
        bool can_run = false;
        int batch_size = 0;
        {
            std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
            can_run = pc_ != nullptr;
            if (can_run) {
                constexpr int kMaximumDatagramsPerBatch = 24;
                constexpr auto kMaximumBatchTime = std::chrono::microseconds(1000);
                const auto batch_started_at = std::chrono::steady_clock::now();
                for (int packet = 0; packet < kMaximumDatagramsPerBatch; ++packet) {
                    if (peer_connection_loop(pc_) == 0)
                        break;
                    batch_size++;
                    if (batch_size >= 8 && std::chrono::steady_clock::now() - batch_started_at >= kMaximumBatchTime)
                        break;
                }
            }
        }
        const int backoff_ms = opennow::network::LoopBackoffMilliseconds(can_run, batch_size);
        if (backoff_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        else
            std::this_thread::yield();
    }
}

void WebRtcSession::poll() {
    if (stop_requested_.load(std::memory_order_acquire))
        return;

    control_channel_.Poll();

    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    if (pc_) {
        const PeerConnectionState state = peer_connection_get_state(pc_);
        current_state_ = std::string("Peer ") + peer_connection_state_to_string(state);
        if (state == PEER_CONNECTION_COMPLETED && !initial_keyframe_requested_) {
            last_startup_keyframe_request_ = std::chrono::steady_clock::now();
            request_keyframe("stream_start");
            initial_keyframe_requested_ = true;
        }
        maybe_request_startup_keyframe_retry();
        maybe_recover_decode_stall();
    }
}

void WebRtcSession::request_stop() {
    if (stop_requested_.exchange(true, std::memory_order_acq_rel))
        return;
    network_running_.store(false, std::memory_order_release);
    decoder_running_.store(false, std::memory_order_release);
    decoder_queue_cv_.notify_all();
}

void WebRtcSession::stop() {
    request_stop();
    stop_ice_poll_worker();
    if (network_thread_.joinable())
        network_thread_.join();

    if (audio_)
        audio_->stop();
    if (decoder_thread_.joinable())
        decoder_thread_.join();

    {
        std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
        clear_decoder_queue_locked();
        decoder_buffer_pool_.clear();
    }

    control_channel_.Disconnect();
    control_channel_alive_.store(false, std::memory_order_relaxed);

    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    if (pc_) {
        peer_connection_close(pc_);
        peer_connection_destroy(pc_);
        pc_ = nullptr;
    }
    if (renderer_)
        renderer_.reset();
    if (decoder_) {
        decoder_->cleanup();
        decoder_.reset();
    }
    std::remove(kNvdecActiveMarker);
}

namespace {

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        std::string line = text.substr(start, end - start);
        while (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            lines.push_back(std::move(line));
        start = end + 1;
    }
    return lines;
}

} // namespace

// libpeer's local ICE gathering is SYNCHRONOUS: by the time
// peer_connection_create_offer() returns, all locally reachable candidates
// are already gathered and this callback has fired once with the complete
// assembled SDP (NOT per-candidate trickle, unlike a browser). Extract each
// "a=candidate:" line and treat it as one local candidate to trickle-POST via
// addIceCandidate — buffered until the answer is set (ice_can_send_),
// matching webrtcstreamer.js's own "earlyCandidates" buffering.
void WebRtcSession::on_ice_candidate(const std::string& sdp) {
    const bool looks_like_full_sdp = sdp.find("v=0") != std::string::npos || sdp.find("m=") != std::string::npos;
    std::vector<std::string> candidate_lines;
    if (looks_like_full_sdp) {
        for (const auto& line : SplitLines(sdp)) {
            if (line.rfind("a=candidate:", 0) == 0)
                candidate_lines.push_back(line.substr(2)); // Strip "a=", keep "candidate:...".
        }
    } else if (!sdp.empty()) {
        candidate_lines.push_back(sdp.rfind("a=", 0) == 0 ? sdp.substr(2) : sdp);
    }

    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    for (const auto& candidate : candidate_lines) {
        AppendTraceLog("LOCAL candidate " + candidate);
        if (ice_can_send_ && signaling_) {
            signaling_->SendIceCandidate(candidate, "0", 0);
        } else {
            pending_local_ice_.push_back({candidate, "0", 0});
        }
    }
}

void WebRtcSession::on_peer_state_change(PeerConnectionState state) {
    current_state_ = std::string("Peer ") + peer_connection_state_to_string(state);
    AppendStreamLog("PEER state " + std::string(peer_connection_state_to_string(state)));
}

std::string WebRtcSession::get_debug_info() const {
    std::string info = "state=" + current_state_;
    info += " packets=" + std::to_string(packets_received_.load());
    info += " frames=" + std::to_string(frames_decoded_.load());
    info += " decodeErrors=" + std::to_string(decode_errors_.load());
    info += " controlAlive=" + std::to_string(control_channel_alive_.load() ? 1 : 0);
    return info;
}

// MARK: - libpeer callback trampolines (declared extern "C" in internal.hpp)

namespace opennow::webrtc::internal {

void on_ice_candidate_cb(char* sdp_text, void* userdata) {
    if (userdata && sdp_text)
        static_cast<WebRtcSession*>(userdata)->on_ice_candidate(std::string(sdp_text));
}

void on_peer_state_change_cb(PeerConnectionState state, void* userdata) {
    if (userdata)
        static_cast<WebRtcSession*>(userdata)->on_peer_state_change(state);
}

void on_video_packet_cb(const PeerVideoPacket* packet, void* userdata) {
    if (userdata && packet)
        static_cast<WebRtcSession*>(userdata)->on_video_packet(*packet);
}

void on_audio_packet_cb(const PeerAudioPacket* packet, void* userdata) {
    if (userdata && packet)
        static_cast<WebRtcSession*>(userdata)->on_audio_packet(*packet);
}

void on_rtp_sender_report_cb(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp, void* userdata) {
    if (userdata)
        static_cast<WebRtcSession*>(userdata)->on_rtp_sender_report(ssrc, ntp_us, rtp_timestamp);
}

} // namespace opennow::webrtc::internal
