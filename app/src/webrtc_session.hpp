#pragma once
#include "models.hpp"
#include "stream_settings.hpp"
#include "stream_end_policy.hpp"
#include "boosteroid_signaling_client.hpp"
#include "boosteroid_control_channel.hpp"
#include <string>
#include <memory>
#include <atomic>
#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <vector>
#include <chrono>
#include "peer_connection.h"
#include "peer.h"
#include "stream/ffmpeg/FFmpegVideoDecoder.hpp"
#include "stream/IVideoRenderer.hpp"
#if defined(USE_DK_RENDERER)
#include "stream/deko3d/DKVideoRenderer.hpp"
#else
#include "stream/OpenGL/GLVideoRenderer.hpp"
#endif
#include "stream/audio/AudioPipeline.hpp"

// MARK: - WebRtcSession (Nintendo Switch port, Boosteroid protocol)
//
// Adapted from SwitchNOW's WebRtcSession (see
// app/src/_legacy_gfn_reference/webrtc_session.hpp for the GFN original). The
// low-level pieces — libpeer peer connection, FFmpeg decode, Deko3D/OpenGL
// render, RTP/audio pipeline, decode-queue/backpressure handling — are
// PROTOCOL-AGNOSTIC and reused near-verbatim (see webrtc/media.cpp, which
// barely changed). What's DIFFERENT, per BoosteroidATV's CONFIRMED protocol
// (see StreamController.swift's header comment):
//   - Signaling is REST (BoosteroidSignalingClient: getIceServers/getParams/
//     call/addIceCandidate/getIceCandidate polling), not a persistent
//     WebSocket. There's no "connect a socket, wait for an offer" step —
//     THIS CLIENT creates the offer (client-is-offerer, unlike GFN).
//   - A separate control WebSocket (BoosteroidControlChannel) must be opened
//     FIRST — it claims the session and gates when WebRTC signaling may
//     start (wait for its WebrtcEngineReady/SessionActive event).
//   - ALL input rides that control channel as JSON frames, not a WebRTC
//     datachannel. GFN's ClientDataChannel-style datachannel handling is
//     dropped (Boosteroid's own webrtcstreamer.js does open one, but a live
//     capture found zero input traffic on it — see the tvOS app's
//     StreamController.swift didOpen dataChannel comment).
class WebRtcSession {
public:
    // `cookies` are needed by both the REST signaling client (Origin/Referer/
    // Cookie auth against the per-session node host — CONFIRMED necessary,
    // see BoosteroidSignalingClient) and are otherwise unused here.
    WebRtcSession(
        const std::string& node_base_url,
        const std::string& session_id,
        const std::string& query_string,
        const opennow::CookieJar& cookies,
        const opennow::StreamSettings& settings);
    ~WebRtcSession();

    void start();
    void request_stop();
    void stop();
    void poll();

    // MARK: Input — routes to BoosteroidControlChannel, NOT a datachannel.
    // Shapes/indices CONFIRMED — see boosteroid_control_channel.hpp.
    void send_key_event(bool down, uint16_t vk, uint16_t scancode, uint16_t modifiers);
    void send_mouse_move(int16_t dx, int16_t dy);
    void send_mouse_absolute(int x, int y); // Pixels in the streamed surface; converted to the server's 0-1 fraction.
    void send_mouse_button(bool down, uint8_t button);
    void send_mouse_wheel(int16_t delta);
    void controller_connect(int local_index, const std::string& vendor_name);
    void controller_disconnect(int local_index);
    // `buttons`/`axes` follow CONFIRMED Boosteroid indices (0-9 buttons,
    // 0-5 axes incl. triggers) — see InputSender.swift's pollGamepad for the
    // reference mapping this is meant to match. Called every input-poll tick
    // by the UI layer (StreamView) with the CURRENT full state; this class
    // does its own change-detection so the socket isn't flooded.
    void controller_update(
        int local_index,
        const std::array<bool, 10>& buttons,
        const std::array<float, 6>& axes, // [-1,1], trigger axes (2,5) are [0,1]
        int dpad_hat_bitmask);

    void updateSurfaceSize(int width, int height); // Keeps absolute-mouse math in step with the ACTUAL decoded resolution.

    int stream_width() const {
        const int rendered = rendered_video_width_.load(std::memory_order_relaxed);
        return rendered > 0 ? rendered : settings_.width;
    }
    int stream_height() const {
        const int rendered = rendered_video_height_.load(std::memory_order_relaxed);
        return rendered > 0 ? rendered : settings_.height;
    }
    void draw(NVGcontext* vg, int width, int height, AVFrame* frame, uint64_t generation);

    // Callbacks from libpeer (see webrtc/internal.hpp's extern "C" trampolines).
    void on_ice_candidate(const std::string& sdp);
    void on_peer_state_change(PeerConnectionState state);
    void on_video_packet(const PeerVideoPacket& packet);
    void on_audio_packet(const PeerAudioPacket& packet);
    void on_rtp_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp);
    int64_t video_target_rtp_timestamp() const;

    std::string get_debug_info() const;
    std::string current_state() const { return current_state_; }
    bool is_control_channel_alive() const { return control_channel_alive_.load(std::memory_order_relaxed); }
    bool got_video_track() const { return got_video_track_.load(std::memory_order_relaxed); }

private:
    std::string node_base_url_;
    std::string session_id_;
    std::string query_string_;
    opennow::CookieJar cookies_;
    opennow::StreamSettings settings_;

    std::unique_ptr<opennow::BoosteroidSignalingClient> signaling_;
    opennow::BoosteroidControlChannel control_channel_;
    std::atomic<bool> control_channel_alive_ {false};
    std::atomic<bool> webrtc_started_ {false};

    // Per-controller server-assigned id (CONFIRMED required before the server
    // accepts button/axes/pad for that controller) plus provisional-id/change
    // -detection state, keyed by the local controller index (0..7 on Switch).
    struct ControllerState {
        bool connected = false;
        bool acked = false;
        int server_id = -1; // Provisional: local_index until the ack arrives.
        std::string pending_name;
        std::array<bool, 10> last_buttons {};
        std::array<int, 6> last_axes {};
        int last_hat = 0;
    };
    std::array<ControllerState, 8> controllers_ {};
    int mouse_surface_width_ = 0;
    int mouse_surface_height_ = 0;
    bool mouse_connected_sent_ = false;

    struct PeerConnection* pc_ = nullptr;
    std::unique_ptr<FFmpegVideoDecoder> decoder_;
    std::unique_ptr<IVideoRenderer> renderer_;
    std::atomic<int> rendered_video_width_ {0};
    std::atomic<int> rendered_video_height_ {0};
    std::unique_ptr<AudioPipeline> audio_;
    int decoder_setup_result_ = -1;
    bool decoder_fallback_used_ = false;
    std::string video_backend_name_ = "uninitialized";

    std::thread network_thread_;
    std::atomic<bool> network_running_ {false};
    std::atomic<bool> stop_requested_ {false};
    std::atomic<bool> got_video_track_ {false};

    struct DecodeUnit {
        std::vector<uint8_t> data;
        bool idr = false;
        uint32_t rtp_timestamp = 0;
        std::chrono::steady_clock::time_point enqueued_at {};
    };
    std::thread decoder_thread_;
    std::atomic<bool> decoder_running_ {false};
    std::atomic<bool> decoder_resync_required_ {false};
    std::atomic<bool> decoder_reset_requested_ {false};
    std::atomic<bool> keyframe_needed_ {false};
    mutable std::mutex decoder_queue_mutex_;
    std::condition_variable decoder_queue_cv_;
    std::deque<DecodeUnit> decoder_queue_;
    std::deque<std::vector<uint8_t>> decoder_buffer_pool_;
    std::atomic<int> decoder_queue_drops_ {0};
    std::atomic<uint64_t> decoder_buffer_reuses_ {0};
    std::atomic<uint64_t> decoder_buffer_allocations_ {0};

    // ICE trickle buffering, matching StreamController.swift's iceCanSend:
    // local candidates gathered before the offer/answer exchange completes
    // are buffered and flushed only after the answer is set.
    bool ice_can_send_ = false;
    struct PendingIce { std::string sdp; std::string sdp_mid; int sdp_mline_index; };
    std::vector<PendingIce> pending_local_ice_;
    std::thread ice_poll_thread_;
    std::atomic<bool> ice_poll_running_ {false};
    // getIceCandidate is a POLL, not a queue that drains — it returns the
    // node's FULL current candidate list on every call (CONFIRMED on real
    // hardware 2026-07-31: an identical 571-byte body every single second).
    // Feeding that straight into peer_connection_add_ice_candidate re-adds
    // the same candidates once per second, and libpeer's remote table is a
    // fixed 10 entries (AGENT_MAX_CANDIDATES) that it never dedupes — so it
    // overflowed within seconds ("Remote ICE candidate table is full"),
    // while also filling the 100-entry candidate-pair table with duplicate
    // pairs. Track what's already been added and skip repeats.
    std::set<std::string> added_remote_ice_;
    // MUST outlive pc_. libpeer's IceServer holds bare `const char*` and
    // peer_connection_create() only does a shallow memcpy of the config —
    // it dereferences those pointers much later, inside
    // peer_connection_create_offer(). Keeping the strings in a member (not a
    // local in setup_peer_connection) is what makes that safe.
    std::vector<opennow::IceServerInfo> ice_servers_;
    // When the first stream/* burst arrived. Used to hold off the fallback
    // "start WebRTC anyway" path until the server has had a fair chance to
    // send the real settings/webrtc signal — see handle_control_event.
    std::chrono::steady_clock::time_point first_session_active_at_ {};

    std::string current_state_ = "Initializing";
    std::atomic<int> packets_received_ {0};
    std::atomic<int> frames_decoded_ {0};
    std::atomic<int> decode_errors_ {0};
    std::atomic<int64_t> last_rendered_video_pts_ {AV_NOPTS_VALUE};
    std::atomic<uint64_t> video_access_unit_bytes_ {0};
    std::atomic<uint64_t> presented_frames_ {0};
    std::atomic<uint64_t> last_presented_generation_ {0};
    std::atomic<size_t> decoder_queue_high_water_ {0};
    bool first_video_packet_logged_ = false;
    bool first_decoded_frame_logged_ = false;
    int last_logged_packet_count_ = 0;
    std::atomic<int> last_logged_frame_count_ {0};
    std::atomic<int> last_logged_decode_error_count_ {0};
    std::chrono::steady_clock::time_point session_started_at_ {};
    std::atomic<uint64_t> last_decoded_frame_at_us_ {0};
    std::atomic<uint64_t> last_video_packet_at_us_ {0};
    std::chrono::steady_clock::time_point last_decode_stall_request_at_ {};
    std::chrono::steady_clock::time_point last_keyframe_request_at_ {};
    int keyframe_request_attempts_ = 0;
    bool initial_keyframe_requested_ = false;
    std::chrono::steady_clock::time_point last_startup_keyframe_request_ {};
    mutable std::recursive_mutex peer_mutex_;

    struct SenderReport { uint32_t ssrc = 0; uint64_t ntp_us = 0; uint32_t rtp_timestamp = 0; };
    std::vector<SenderReport> sender_reports_;
    uint32_t audio_ssrc_ = 0;
    uint32_t video_ssrc_ = 0;
    uint64_t video_sr_ntp_us_ = 0;
    uint32_t video_sr_rtp_timestamp_ = 0;
    bool have_video_sender_report_ = false;

    void setup_peer_connection(const std::vector<opennow::IceServerInfo>& ice_servers);
    void start_network_worker();
    void network_loop();
    void start_decoder_worker();
    void decoder_loop();
    void enqueue_decode_unit(const uint8_t* data, size_t size, uint32_t rtp_timestamp);
    void recycle_decode_buffer_locked(std::vector<uint8_t>&& buffer);
    void clear_decoder_queue_locked();
    void request_keyframe(const char* reason);
    void maybe_recover_decode_stall();
    void maybe_request_startup_keyframe_retry();
    void log_stream_summary(const char* reason);

    // Control-channel-driven flow.
    void handle_control_event(const opennow::BoosteroidControlEvent& event);
    void start_webrtc_media(); // Runs the getIceServers -> getParams -> offer -> call -> ICE chain.
    void start_ice_poll_worker();
    void stop_ice_poll_worker();

    ControllerState* find_or_create_controller(int local_index);
};
