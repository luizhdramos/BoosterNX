#include "boosteroid_control_channel.hpp"
#include "json_utils.hpp"

#include <borealis.hpp>

#include <cstdlib>
#include <sstream>

using namespace opennow::json;

namespace opennow
{
namespace
{

bool IsExternalDeviceType(const std::string& type)
{
    // CONFIRMED literal set from SessionHandler.sendEvents.
    return type == "keyboard" || type == "mouse" || type == "controller" || type == "finger";
}

std::string StripScheme(std::string host)
{
    const std::string https = "https://";
    const std::string http = "http://";
    if (host.rfind(https, 0) == 0)
        host.erase(0, https.size());
    else if (host.rfind(http, 0) == 0)
        host.erase(0, http.size());
    return host;
}

} // namespace

BoosteroidControlChannel::BoosteroidControlChannel() = default;
BoosteroidControlChannel::~BoosteroidControlChannel() { Disconnect(); }

bool BoosteroidControlChannel::Connect(
    const std::string& node_base_url,
    const std::string& query_string,
    int resolution_width,
    int resolution_height,
    int refresh_rate,
    int max_bitrate_bps,
    EventHandler on_event,
    const std::string& language,
    const std::string& os,
    const std::string& dev_type,
    const std::string& client_type)
{
    on_event_ = std::move(on_event);
    status_framerate_ = refresh_rate;
    status_bandwidth_bps_ = max_bitrate_bps;

    std::ostringstream url;
    url << "wss://" << StripScheme(node_base_url) << "/?" << query_string
        << "&x=" << resolution_width << "&y=" << resolution_height
        << "&lang=" << language << "&refreshRate=" << refresh_rate
        << "&rtcEngine=webrtc&clientType=" << client_type
        << "&devType=" << dev_type << "&os=" << os << "&rtcAudio=pcm";

    ws_ = std::make_unique<WebSocketClient>(url.str());
    ws_->set_on_message([this](const std::string& text) { HandleMessage(text); });
    if (!ws_->connect())
    {
        brls::Logger::error("BoosteroidControlChannel: connect failed: {}", ws_->get_last_error());
        ws_.reset();
        return false;
    }
    is_open_ = true;
    return true;
}

void BoosteroidControlChannel::Disconnect()
{
    is_open_ = false;
    if (ws_)
    {
        ws_->disconnect();
        ws_.reset();
    }
}

void BoosteroidControlChannel::Poll()
{
    if (ws_)
        ws_->poll();
}

void BoosteroidControlChannel::SendRaw(const std::string& type, const char* action, json_t* fields)
{
    if (!is_open_ || !ws_)
        return;

    JsonPtr root(json_object(), &json_decref);
    json_object_set_new(root.get(), "type", json_string(type.c_str()));
    if (action)
        json_object_set_new(root.get(), "action", json_string(action));
    if (fields)
    {
        const char* key;
        json_t* value;
        json_object_foreach(fields, key, value)
        {
            json_object_set(root.get(), key, value); // Borrowed: incref's internally.
        }
    }
    if (IsExternalDeviceType(type))
    {
        json_object_set_new(root.get(), "id_cmd", json_integer(++id_cmd_counter_));
        json_object_set_new(root.get(), "from_udp", json_false());
    }

    char* dump = json_dumps(root.get(), JSON_COMPACT);
    if (!dump)
        return;
    ws_->send_message(dump);
    free(dump);
}

void BoosteroidControlChannel::HandleMessage(const std::string& text)
{
    JsonPtr root = TryLoad(text);
    if (!root || !json_is_object(root.get()))
        return;

    const std::string type = GetString(root.get(), "type");
    const std::string action = GetString(root.get(), "action");

    if (!on_event_)
        return;

    // Cursor updates - matched loosely since the exact routing was never
    // isolated (better to over-match than silently drop the only thing that
    // can put a pointer on screen). Mirrors BoosteroidControlChannel.swift.
    json_t* x_field = json_object_get(root.get(), "x");
    json_t* cursor_x_field = json_object_get(root.get(), "cursorX");
    if (type == "cursor" || action == "cursor" || (type == "mouse" && x_field) || cursor_x_field)
    {
        BoosteroidControlEvent event;
        event.type = BoosteroidControlEvent::Type::Cursor;
        json_t* x = x_field ? x_field : cursor_x_field;
        json_t* y = json_object_get(root.get(), "y");
        if (!y)
            y = json_object_get(root.get(), "cursorY");
        if (json_is_integer(x)) { event.has_cursor_x = true; event.cursor_x = static_cast<int>(json_integer_value(x)); }
        if (json_is_integer(y)) { event.has_cursor_y = true; event.cursor_y = static_cast<int>(json_integer_value(y)); }
        on_event_(event);
        return;
    }

    if (type == "controller" && action == "connected")
    {
        json_t* name = json_object_get(root.get(), "name");
        json_t* id = json_object_get(root.get(), "id");
        if (json_is_string(name) && id)
        {
            BoosteroidControlEvent event;
            event.type = BoosteroidControlEvent::Type::ControllerAck;
            event.controller_name = json_string_value(name);
            event.controller_id = AsString(id);
            on_event_(event);
            return;
        }
    }

    if (type == "controller" && action == "rumble")
    {
        BoosteroidControlEvent event;
        event.type = BoosteroidControlEvent::Type::ControllerRumble;
        event.controller_id = GetString(root.get(), "id");
        json_t* left = json_object_get(root.get(), "left");
        json_t* right = json_object_get(root.get(), "right");
        event.rumble_left = json_is_number(left) ? json_number_value(left) : 0;
        event.rumble_right = json_is_number(right) ? json_number_value(right) : 0;
        on_event_(event);
        return;
    }

    // CONFIRMED: settings/webrtc is the "start the WebRTC engine now" signal.
    if (type == "settings" && action == "webrtc")
    {
        BoosteroidControlEvent event;
        event.type = BoosteroidControlEvent::Type::WebrtcEngineReady;
        on_event_(event);
        return;
    }

    // CONFIRMED (the frames-0 fix): after the WebRTC peer connects, the
    // server sends {"type":"stream","action":"getstatus"} and WAITS for the
    // client's readiness reply before it starts sending video. Reply here.
    if (type == "stream" && action == "getstatus")
    {
        SendStatusHandshake();
        BoosteroidControlEvent event;
        event.type = BoosteroidControlEvent::Type::SessionActive;
        on_event_(event);
        return;
    }
    if (type == "stream")
    {
        BoosteroidControlEvent event;
        event.type = BoosteroidControlEvent::Type::SessionActive;
        on_event_(event);
        return;
    }

    BoosteroidControlEvent event;
    event.type = BoosteroidControlEvent::Type::Raw;
    event.raw_type = type;
    event.raw_action = action;
    on_event_(event);
}

void BoosteroidControlChannel::SendStatusHandshake()
{
    {
        JsonPtr fields(json_object(), &json_decref);
        json_object_set_new(fields.get(), "code", json_integer(1033)); // 1033 = en-US LCID
        SendRaw("keyboard", "language", fields.get());
    }
    {
        JsonPtr params(json_object(), &json_decref);
        json_object_set_new(params.get(), "type", json_string("web"));
        json_object_set_new(params.get(), "ver", json_string("v_7.4.17"));
        json_object_set_new(params.get(), "gpu", json_string("NVIDIA Tegra X1, Nintendo Switch"));
        json_object_set_new(params.get(), "proto", json_integer(1));
        json_object_set_new(params.get(), "framerate_max", json_integer(status_framerate_));
        json_object_set_new(params.get(), "cursor_zip", json_false());
        json_object_set_new(params.get(), "filler", json_false());
        json_object_set_new(params.get(), "beta", json_integer(0));
        json_object_set_new(params.get(), "rtcEngine", json_string("webrtc"));
        json_object_set_new(params.get(), "rtcAudio", json_string("pcm"));

        JsonPtr fields(json_object(), &json_decref);
        json_object_set_new(fields.get(), "value", json_string("ok"));
        json_object_set_new(fields.get(), "params", json_incref(params.get()));
        SendRaw("stream", "status", fields.get());
    }
    {
        JsonPtr fields(json_object(), &json_decref);
        json_object_set_new(fields.get(), "value", json_integer(status_framerate_));
        SendRaw("stream", "refreshRate", fields.get());
    }
    // CONFIRMED: max bitrate is a separate {type:"stream",action:"bandwidth",
    // value:<bps>} message (bits/sec), not set via SDP.
    if (status_bandwidth_bps_ > 0)
    {
        JsonPtr fields(json_object(), &json_decref);
        json_object_set_new(fields.get(), "value", json_integer(status_bandwidth_bps_));
        SendRaw("stream", "bandwidth", fields.get());
    }
}

// MARK: - Typed input helpers
//
// CONFIRMED per-type shapes — see this file's header, and
// BoosteroidATV/BoosteroidControlChannel.swift's header comment for the full
// capture-by-capture trail (button/axis indices, the D-pad hat bitmask, the
// ±32767 trigger remap formula).

void BoosteroidControlChannel::SendKeyboardButton(bool is_pressed, int vk_code)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "isPressed", is_pressed ? json_true() : json_false());
    json_object_set_new(fields.get(), "code", json_integer(vk_code));
    SendRaw("keyboard", "button", fields.get());
}

void BoosteroidControlChannel::SendMouseConnected()
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "LeftBtnState", json_false());
    json_object_set_new(fields.get(), "MiddleBtnState", json_false());
    json_object_set_new(fields.get(), "RightBtnState", json_false());
    SendRaw("mouse", "connected", fields.get());
}

void BoosteroidControlChannel::SendMouseMoveRelative(int dx, int dy, int surface_width, int surface_height)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "movementX", json_integer(dx));
    json_object_set_new(fields.get(), "movementY", json_integer(dy));
    json_object_set_new(fields.get(), "surfaceWidth", json_integer(surface_width));
    json_object_set_new(fields.get(), "surfaceHeight", json_integer(surface_height));
    json_object_set_new(fields.get(), "syncLocalPosition", json_false());
    json_object_set_new(fields.get(), "movementIsAdjusted", json_true());
    SendRaw("mouse", nullptr, fields.get()); // No "action" on this shape - CONFIRMED.
}

void BoosteroidControlChannel::SendMouseMoveAbsolute(double fraction_x, double fraction_y)
{
    fraction_x = fraction_x < 0 ? 0 : (fraction_x > 1 ? 1 : fraction_x);
    fraction_y = fraction_y < 0 ? 0 : (fraction_y > 1 ? 1 : fraction_y);
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "X", json_real(fraction_x));
    json_object_set_new(fields.get(), "Y", json_real(fraction_y));
    json_object_set_new(fields.get(), "offsetX", json_integer(0));
    json_object_set_new(fields.get(), "offsetY", json_integer(0));
    json_object_set_new(fields.get(), "isVisible", json_true());
    SendRaw("mouse", "move", fields.get());
}

void BoosteroidControlChannel::SendMouseButton(bool is_pressed, int button)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "isPressed", is_pressed ? json_true() : json_false());
    json_object_set_new(fields.get(), "btn", json_integer(button));
    SendRaw("mouse", "button", fields.get());
}

void BoosteroidControlChannel::SendMouseWheel(int delta_y)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "deltaY", json_integer(delta_y > 0 ? 1 : -1));
    SendRaw("mouse", "wheel", fields.get());
}

void BoosteroidControlChannel::SendControllerConnected(const std::string& name)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "name", json_string(name.c_str()));
    SendRaw("controller", "connected", fields.get());
}

void BoosteroidControlChannel::SendControllerDisconnected(int id)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "id", json_integer(id));
    SendRaw("controller", "disconnected", fields.get());
}

void BoosteroidControlChannel::SendControllerButton(int id, int button_index, int value)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "id", json_integer(id));
    json_object_set_new(fields.get(), "button", json_integer(button_index));
    json_object_set_new(fields.get(), "value", json_integer(value));
    SendRaw("controller", "button", fields.get());
}

void BoosteroidControlChannel::SendControllerAxis(int id, int axis_index, int value)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "id", json_integer(id));
    json_object_set_new(fields.get(), "axes", json_integer(axis_index));
    json_object_set_new(fields.get(), "value", json_integer(value));
    SendRaw("controller", "axes", fields.get());
}

void BoosteroidControlChannel::SendControllerPad(int id, int hat_bitmask)
{
    JsonPtr fields(json_object(), &json_decref);
    json_object_set_new(fields.get(), "id", json_integer(id));
    json_object_set_new(fields.get(), "hat", json_integer(hat_bitmask));
    SendRaw("controller", "pad", fields.get());
}

} // namespace opennow
