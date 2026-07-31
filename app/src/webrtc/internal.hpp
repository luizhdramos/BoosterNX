#pragma once

#include "peer.h"
#include "peer_connection.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace opennow::webrtc::internal
{

bool StartsWith(const std::string& value, const char* prefix);

template <typename T>
void AtomicMax(std::atomic<T>& target, T value)
{
    T previous = target.load(std::memory_order_relaxed);
    while (previous < value &&
           !target.compare_exchange_weak(previous, value, std::memory_order_relaxed)) {
    }
}

std::string HexPreview(const uint8_t* data, size_t size, size_t max_bytes = 12);
bool ContainsH264Idr(const uint8_t* data, size_t size);
uint64_t NowUs();

// File-based flight recorders, gated by opennow::StreamDiagnosticsEnabled().
// Reused near-verbatim from SwitchNOW (webrtc/log.cpp) — genuinely
// protocol-agnostic, just structured logging.
void AppendInputLog(const std::string& line);
void AppendStreamLog(const std::string& line);
void ResetStreamTraceLog();
void AppendTraceLog(const std::string& line);
void AppendTraceBlock(const std::string& title, const std::string& body);
std::string PreviewText(const std::string& value, size_t max_chars = 160);

// libpeer callback trampolines: cast `userdata` back to WebRtcSession* and
// forward. NOTE: unlike SwitchNOW's GFN client, there is no
// on_datachannel_* trampoline here — Boosteroid's "ClientDataChannel" is
// still created (PeerConfiguration::datachannel = DATA_CHANNEL_STRING, so
// its m=application line appears in the offer, matching CONFIRMED server
// expectations) but CONFIRMED to carry no input traffic, so this port
// doesn't register peer_connection_ondatachannel at all — see
// webrtc_session.hpp's header comment.
extern "C" {
void on_ice_candidate_cb(char* sdp_text, void* userdata);
void on_peer_state_change_cb(PeerConnectionState state, void* userdata);
void on_video_packet_cb(const PeerVideoPacket* packet, void* userdata);
void on_audio_packet_cb(const PeerAudioPacket* packet, void* userdata);
void on_rtp_sender_report_cb(uint32_t ssrc, uint64_t ntp_us,
                             uint32_t rtp_timestamp, void* userdata);
}

} // namespace opennow::webrtc::internal
