// Decode queue, video/audio packet handling, RTP sender-report tracking, and
// keyframe recovery. Reused near-verbatim from SwitchNOW's webrtc/media.cpp
// (see app/src/_legacy_gfn_reference/media.cpp is NOT kept separately since
// this file barely changed - just trimmed to the member set webrtc_session.hpp
// now declares, and the JSON-over-signaling-WebSocket keyframe-request
// fallback was dropped: Boosteroid has no persistent signaling socket to send
// it on, and BoosteroidATV's own StreamController.swift has no equivalent
// fallback either — RTCP PLI (peer_connection_request_video_keyframe) is the
// sole mechanism there too). All of this is genuinely protocol-agnostic: it
// only consumes libpeer's RTP/decode callbacks, regardless of who signaled.
#include "webrtc_session.hpp"
#include "stream/audio/AudioRtpUtils.hpp"
#include "stream/DecodeQueuePolicy.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "internal.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

using namespace opennow::webrtc::internal;

void WebRtcSession::start_decoder_worker() {
    if (decoder_running_.exchange(true))
        return;
    decoder_thread_ = std::thread(&WebRtcSession::decoder_loop, this);
    AppendStreamLog("DECODE worker_started");
}

void WebRtcSession::decoder_loop() {
    for (;;) {
        DecodeUnit unit;
        {
            std::unique_lock<std::mutex> lock(decoder_queue_mutex_);
            decoder_queue_cv_.wait(lock, [this] {
                return !decoder_running_.load(std::memory_order_acquire) || !decoder_queue_.empty();
            });
            if (!decoder_running_.load(std::memory_order_acquire))
                break;
            unit = std::move(decoder_queue_.front());
            decoder_queue_.pop_front();
        }

        if (decoder_reset_requested_.exchange(false) && decoder_)
            decoder_->reset_stream();

        const int decoded = decoder_
            ? decoder_->submit_decode_unit(unit.data.data(), static_cast<int>(unit.data.size()), unit.rtp_timestamp)
            : -1;

        const int access_unit_count = packets_received_.load();
        if (decoded > 0) {
            last_decoded_frame_at_us_.store(NowUs(), std::memory_order_release);
            const int frame_count = frames_decoded_.fetch_add(decoded) + decoded;
            if (!first_decoded_frame_logged_) {
                first_decoded_frame_logged_ = true;
                AppendStreamLog("DECODE first_frame frames=" + std::to_string(frame_count) +
                                " accessUnits=" + std::to_string(access_unit_count));
            } else if (frame_count - last_logged_frame_count_.load(std::memory_order_relaxed) >= 120) {
                AppendStreamLog("DECODE frame_progress frames=" + std::to_string(frame_count) +
                                " accessUnits=" + std::to_string(access_unit_count));
                last_logged_frame_count_.store(frame_count, std::memory_order_relaxed);
            }
        } else if (decoded < 0) {
            const int error_count = decode_errors_.fetch_add(1) + 1;
            if (error_count <= 5 || error_count - last_logged_decode_error_count_.load(std::memory_order_relaxed) >= 20) {
                AppendStreamLog("DECODE error count=" + std::to_string(error_count) +
                                " accessUnit=" + std::to_string(access_unit_count) +
                                " size=" + std::to_string(unit.data.size()) +
                                " idr=" + std::to_string(unit.idr ? 1 : 0) +
                                " preview=" + HexPreview(unit.data.data(), unit.data.size()));
                last_logged_decode_error_count_.store(error_count, std::memory_order_relaxed);
            }
            decoder_resync_required_.store(true);
            if (!decoder_ || !decoder_->uses_hardware_frames())
                decoder_reset_requested_.store(true);
            keyframe_needed_.store(true);
        }

        if (unit.idr && decoded >= 0) {
            decoder_resync_required_.store(false);
            keyframe_needed_.store(false);
        }

        {
            std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
            recycle_decode_buffer_locked(std::move(unit.data));
        }
    }
    AppendStreamLog("DECODE worker_stopped");
}

void WebRtcSession::recycle_decode_buffer_locked(std::vector<uint8_t>&& buffer) {
    constexpr size_t kPoolLimit = 8;
    constexpr size_t kMaximumReusableCapacity = 2 * 1024 * 1024;
    if (decoder_buffer_pool_.size() >= kPoolLimit || buffer.capacity() > kMaximumReusableCapacity)
        return;
    buffer.clear();
    decoder_buffer_pool_.push_back(std::move(buffer));
}

void WebRtcSession::clear_decoder_queue_locked() {
    while (!decoder_queue_.empty()) {
        recycle_decode_buffer_locked(std::move(decoder_queue_.front().data));
        decoder_queue_.pop_front();
    }
}

void WebRtcSession::enqueue_decode_unit(const uint8_t* data, size_t size, uint32_t rtp_timestamp) {
    if (!data || size == 0 || !decoder_running_.load(std::memory_order_acquire))
        return;

    const bool idr = ContainsH264Idr(data, size);
    std::lock_guard<std::mutex> lock(decoder_queue_mutex_);

    if (decoder_resync_required_.load() && !idr) {
        decoder_queue_drops_++;
        return;
    }

    const size_t kMaxQueuedAccessUnits = opennow::video::MaximumQueuedAccessUnits(settings_.fps);
    if (decoder_queue_.size() >= kMaxQueuedAccessUnits) {
        decoder_queue_drops_ += static_cast<int>(decoder_queue_.size());
        clear_decoder_queue_locked();
        decoder_resync_required_.store(true);
        if (!decoder_ || !decoder_->uses_hardware_frames())
            decoder_reset_requested_.store(true);
        keyframe_needed_.store(true);
        if (!idr) {
            decoder_queue_drops_++;
            return;
        }
    }

    std::vector<uint8_t> buffer;
    if (!decoder_buffer_pool_.empty()) {
        buffer = std::move(decoder_buffer_pool_.front());
        decoder_buffer_pool_.pop_front();
        decoder_buffer_reuses_.fetch_add(1, std::memory_order_relaxed);
    } else {
        decoder_buffer_allocations_.fetch_add(1, std::memory_order_relaxed);
    }
    buffer.resize(size);
    std::memcpy(buffer.data(), data, size);
    decoder_queue_.push_back({std::move(buffer), idr, rtp_timestamp, std::chrono::steady_clock::now()});
    AtomicMax(decoder_queue_high_water_, decoder_queue_.size());
    decoder_queue_cv_.notify_one();
}

void WebRtcSession::draw(NVGcontext* vg, int width, int height, AVFrame* frame, uint64_t generation) {
    if (renderer_ && frame) {
        rendered_video_width_.store(frame->width, std::memory_order_relaxed);
        rendered_video_height_.store(frame->height, std::memory_order_relaxed);
        last_rendered_video_pts_.store(frame->pts);
        renderer_->drawLatest(vg, width, height, frame, VIDEO_FORMAT_H264, generation);
        const uint64_t previous_generation = last_presented_generation_.exchange(generation);
        if (generation != 0 && generation != previous_generation)
            presented_frames_.fetch_add(1, std::memory_order_relaxed);
    }
}

void WebRtcSession::maybe_request_startup_keyframe_retry() {
    if (!pc_ || packets_received_.load() > 0)
        return;
    if (peer_connection_get_state(pc_) != PEER_CONNECTION_COMPLETED)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (last_startup_keyframe_request_.time_since_epoch().count() != 0 &&
        now - last_startup_keyframe_request_ < std::chrono::seconds(3)) {
        return;
    }
    if (keyframe_request_attempts_ >= 5)
        return;

    last_startup_keyframe_request_ = now;
    request_keyframe("startup_no_video");
}

void WebRtcSession::maybe_recover_decode_stall() {
    const uint64_t last_decoded_us = last_decoded_frame_at_us_.load(std::memory_order_acquire);
    if (!pc_ || frames_decoded_.load() == 0 || last_decoded_us == 0)
        return;

    const auto now = std::chrono::steady_clock::now();
    const uint64_t now_us = NowUs();
    const uint64_t stalled_ms = now_us >= last_decoded_us ? (now_us - last_decoded_us) / 1000 : 0;
    if (stalled_ms < 1500)
        return;
    if (last_decode_stall_request_at_.time_since_epoch().count() != 0 &&
        now - last_decode_stall_request_at_ < std::chrono::seconds(2)) {
        return;
    }
    if (keyframe_request_attempts_ >= 12)
        return;

    last_decode_stall_request_at_ = now;
    AppendStreamLog("DECODE timeoutMs=" + std::to_string(stalled_ms) +
                    " packets=" + std::to_string(packets_received_.load()) +
                    " frames=" + std::to_string(frames_decoded_.load()));
    {
        std::lock_guard<std::mutex> queue_lock(decoder_queue_mutex_);
        decoder_queue_drops_ += static_cast<int>(decoder_queue_.size());
        clear_decoder_queue_locked();
    }
    decoder_resync_required_.store(true);
    if (!decoder_ || !decoder_->uses_hardware_frames())
        decoder_reset_requested_.store(true);
    request_keyframe("decode_timeout");
}

void WebRtcSession::request_keyframe(const char* reason) {
    const auto now = std::chrono::steady_clock::now();
    constexpr auto kMinimumPliInterval = std::chrono::milliseconds(1500);
    if (last_keyframe_request_at_.time_since_epoch().count() != 0 &&
        now - last_keyframe_request_at_ < kMinimumPliInterval) {
        if (decoder_resync_required_.load())
            keyframe_needed_.store(true);
        return;
    }

    last_keyframe_request_at_ = now;
    keyframe_request_attempts_++;
    const int pli_result = pc_ ? peer_connection_request_video_keyframe(pc_) : -1;
    AppendStreamLog("KEYFRAME request reason=" + std::string(reason ? reason : "unknown") +
                    " attempt=" + std::to_string(keyframe_request_attempts_) +
                    " rtcpPli=" + std::to_string(pli_result) +
                    " packets=" + std::to_string(packets_received_.load()) +
                    " frames=" + std::to_string(frames_decoded_.load()));
}

void WebRtcSession::on_video_packet(const PeerVideoPacket& packet) {
    const uint8_t* data = packet.data;
    const size_t size = packet.size;
    last_video_packet_at_us_.store(NowUs(), std::memory_order_release);
    video_access_unit_bytes_.fetch_add(size, std::memory_order_relaxed);
    video_ssrc_ = packet.ssrc;
    for (const auto& report : sender_reports_) {
        if (report.ssrc == video_ssrc_) {
            video_sr_ntp_us_ = report.ntp_us;
            video_sr_rtp_timestamp_ = report.rtp_timestamp;
            have_video_sender_report_ = true;
            break;
        }
    }
    const int packet_count = packets_received_.fetch_add(1) + 1;
    if (!first_video_packet_logged_) {
        first_video_packet_logged_ = true;
        AppendStreamLog("VIDEO first_access_unit size=" + std::to_string(size) +
                        " idr=" + std::to_string(ContainsH264Idr(data, size) ? 1 : 0) +
                        " preview=" + HexPreview(data, size));
    } else if (packet_count - last_logged_packet_count_ >= 120) {
        AppendStreamLog("VIDEO access_unit_progress units=" + std::to_string(packet_count) +
                        " frames=" + std::to_string(frames_decoded_.load()) +
                        " decodeErrors=" + std::to_string(decode_errors_.load()) +
                        " queueDrops=" + std::to_string(decoder_queue_drops_.load()));
        last_logged_packet_count_ = packet_count;
    }

    got_video_track_.store(true, std::memory_order_relaxed);
    enqueue_decode_unit(data, size, packet.timestamp);
}

void WebRtcSession::on_audio_packet(const PeerAudioPacket& packet) {
    const bool new_audio_ssrc = audio_ssrc_ != packet.ssrc;
    audio_ssrc_ = packet.ssrc;
    if (audio_) {
        audio_->submit(packet);
        if (new_audio_ssrc) {
            for (const auto& report : sender_reports_) {
                if (report.ssrc == audio_ssrc_) {
                    audio_->set_sender_report(report.ssrc, report.ntp_us, report.rtp_timestamp);
                    break;
                }
            }
        }
    }
}

void WebRtcSession::on_rtp_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp) {
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    auto found = std::find_if(sender_reports_.begin(), sender_reports_.end(), [ssrc](const SenderReport& item) {
        return item.ssrc == ssrc;
    });
    if (found == sender_reports_.end())
        sender_reports_.push_back({ssrc, ntp_us, rtp_timestamp});
    else
        *found = {ssrc, ntp_us, rtp_timestamp};

    if (ssrc == audio_ssrc_ && audio_)
        audio_->set_sender_report(ssrc, ntp_us, rtp_timestamp);
    if (ssrc == video_ssrc_) {
        video_sr_ntp_us_ = ntp_us;
        video_sr_rtp_timestamp_ = rtp_timestamp;
        have_video_sender_report_ = true;
    }
}

int64_t WebRtcSession::video_target_rtp_timestamp() const {
    std::lock_guard<std::recursive_mutex> lock(peer_mutex_);
    if (!audio_ || !have_video_sender_report_)
        return AV_NOPTS_VALUE;
    const int64_t audio_ntp = audio_->playback_ntp_us();
    if (audio_ntp < 0)
        return AV_NOPTS_VALUE;
    return opennow::audio::RtpTimestampAtNtp(video_sr_rtp_timestamp_, static_cast<int64_t>(video_sr_ntp_us_), audio_ntp, 90000);
}

void WebRtcSession::updateSurfaceSize(int width, int height) {
    if (width > 0 && height > 0) {
        mouse_surface_width_ = width;
        mouse_surface_height_ = height;
    }
}
