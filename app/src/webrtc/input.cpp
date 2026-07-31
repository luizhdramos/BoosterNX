// Input: keyboard/mouse/controller, all routed through BoosteroidControlChannel
// as JSON text frames — CONFIRMED there is no WebRTC-datachannel input path
// for Boosteroid (unlike GFN, which this replaces). Shapes/indices mirror
// BoosteroidATV's InputSender.swift exactly; see
// boosteroid_control_channel.hpp for the confirmed protocol details this is
// built on.
#include "webrtc_session.hpp"
#include "internal.hpp"

#include <algorithm>
#include <cmath>

using namespace opennow::webrtc::internal;

namespace {
constexpr double kMaxAxis = 32767.0;
// Same threshold the real web client uses before re-sending an axis value
// (out of the ±32767 range) — avoids flooding the socket with stick jitter.
constexpr int kAxisChangeThreshold = 1200;
} // namespace

void WebRtcSession::send_key_event(bool down, uint16_t vk, uint16_t /*scancode*/, uint16_t /*modifiers*/) {
    control_channel_.SendKeyboardButton(down, static_cast<int>(vk));
}

void WebRtcSession::send_mouse_move(int16_t dx, int16_t dy) {
    if (!mouse_connected_sent_) {
        control_channel_.SendMouseConnected();
        mouse_connected_sent_ = true;
    }
    const int width = mouse_surface_width_ > 0 ? mouse_surface_width_ : stream_width();
    const int height = mouse_surface_height_ > 0 ? mouse_surface_height_ : stream_height();
    control_channel_.SendMouseMoveRelative(dx, dy, width, height);
}

void WebRtcSession::send_mouse_absolute(int x, int y) {
    if (!mouse_connected_sent_) {
        control_channel_.SendMouseConnected();
        mouse_connected_sent_ = true;
    }
    const int width = mouse_surface_width_ > 0 ? mouse_surface_width_ : stream_width();
    const int height = mouse_surface_height_ > 0 ? mouse_surface_height_ : stream_height();
    const double fx = width > 0 ? std::clamp(static_cast<double>(x) / width, 0.0, 1.0) : 0.0;
    const double fy = height > 0 ? std::clamp(static_cast<double>(y) / height, 0.0, 1.0) : 0.0;
    control_channel_.SendMouseMoveAbsolute(fx, fy);
}

void WebRtcSession::send_mouse_button(bool down, uint8_t button) {
    if (!mouse_connected_sent_) {
        control_channel_.SendMouseConnected();
        mouse_connected_sent_ = true;
    }
    control_channel_.SendMouseButton(down, static_cast<int>(button));
}

void WebRtcSession::send_mouse_wheel(int16_t delta) {
    control_channel_.SendMouseWheel(static_cast<int>(delta));
}

WebRtcSession::ControllerState* WebRtcSession::find_or_create_controller(int local_index) {
    if (local_index < 0 || local_index >= static_cast<int>(controllers_.size()))
        return nullptr;
    return &controllers_[static_cast<size_t>(local_index)];
}

void WebRtcSession::controller_connect(int local_index, const std::string& vendor_name) {
    ControllerState* controller = find_or_create_controller(local_index);
    if (!controller || controller->connected)
        return;
    controller->connected = true;
    controller->acked = false;
    controller->server_id = local_index; // Provisional id: send immediately, upgrade on ack.
    // CONFIRMED shape: "${gamepad-like-name}#${index}" is just a correlation
    // token the server echoes back — any reasonably unique string works.
    controller->pending_name = vendor_name + "#" + std::to_string(local_index);
    controller->last_buttons.fill(false);
    controller->last_axes.fill(0);
    controller->last_axes[2] = static_cast<int>(-kMaxAxis); // Triggers idle at -32767, not 0.
    controller->last_axes[5] = static_cast<int>(-kMaxAxis);
    controller->last_hat = 0;
    AppendInputLog("controller connect index=" + std::to_string(local_index) + " name=" + controller->pending_name);
    control_channel_.SendControllerConnected(controller->pending_name);
}

void WebRtcSession::controller_disconnect(int local_index) {
    ControllerState* controller = find_or_create_controller(local_index);
    if (!controller || !controller->connected)
        return;
    control_channel_.SendControllerDisconnected(controller->server_id);
    AppendInputLog("controller disconnect index=" + std::to_string(local_index));
    *controller = ControllerState{};
}

void WebRtcSession::controller_update(
    int local_index,
    const std::array<bool, 10>& buttons,
    const std::array<float, 6>& axes,
    int dpad_hat_bitmask) {
    ControllerState* controller = find_or_create_controller(local_index);
    if (!controller || !controller->connected)
        return;
    const int id = controller->server_id;

    for (size_t i = 0; i < buttons.size(); ++i) {
        if (controller->last_buttons[i] != buttons[i]) {
            controller->last_buttons[i] = buttons[i];
            control_channel_.SendControllerButton(id, static_cast<int>(i), buttons[i] ? 1 : 0);
        }
    }

    for (size_t i = 0; i < axes.size(); ++i) {
        int scaled;
        if (i == 2 || i == 5) {
            // Triggers: [0,1] linearly remapped onto the same ±32767 range as
            // sticks — CONFIRMED formula, see boosteroid_control_channel.hpp.
            scaled = static_cast<int>(std::lround(static_cast<double>(axes[i]) * kMaxAxis * 2.0) - static_cast<long>(kMaxAxis));
        } else {
            const float deadzoned = std::abs(axes[i]) < static_cast<float>(settings_.controller_deadzone) ? 0.0f : axes[i];
            scaled = static_cast<int>(std::lround(static_cast<double>(deadzoned) * kMaxAxis));
        }
        if (std::abs(scaled - controller->last_axes[i]) > kAxisChangeThreshold) {
            controller->last_axes[i] = scaled;
            control_channel_.SendControllerAxis(id, static_cast<int>(i), scaled);
        }
    }

    if (controller->last_hat != dpad_hat_bitmask) {
        controller->last_hat = dpad_hat_bitmask;
        control_channel_.SendControllerPad(id, dpad_hat_bitmask);
    }
}
