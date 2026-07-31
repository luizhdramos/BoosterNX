#pragma once

#include "WebSocketClient.hpp"

#include <jansson.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

// MARK: - Boosteroid Control WebSocket (Nintendo Switch port)
//
// Ported from BoosteroidATV's BoosteroidControlChannel.swift — read that
// file's header comment for the FULL confirmed protocol (dates, capture
// method); this port implements exactly the same wire behavior. Summary:
//
//   1. Opening this socket CLAIMS THE SESSION FOR THIS DEVICE (a "switch
//      device" server-side, kicking any browser/other client that had it).
//   2. The server then PUSHES session config: settings/udpforward (unused,
//      raw-UDP path this app doesn't implement), settings/streamIds
//      (resolution), and a stream/* burst (bitrate/framerate/key).
//   3. It GATES WebRTC: only after `{"type":"settings","action":"webrtc"}`
//      arrives on THIS socket should the WebRTC signaling chain
//      (boosteroid_signaling_client) start. Correct order: connect this
//      socket first, wait for settings/webrtc, THEN signal.
//   4. It carries ALL input (keyboard/mouse/controller) as JSON text frames —
//      entirely separate from the WebRTC media path. There is NO input
//      datachannel, unlike GFN.
//
// CONFIRMED URL: wss://{nodeHost}/?{queryString}&x={w}&y={h}&lang={lang}
//   &refreshRate={rate}&rtcEngine=webrtc&clientType={web|native|controller}
//   &devType={desktop|mobile|tv|...}&os={win|lin|mac|a|atv|...}&rtcAudio=pcm
// `clientType` DETERMINES THE VIDEO TRANSPORT (CONFIRMED live): "native"
// makes the server push raw UDP (settings/udpforward, never settings/webrtc);
// "web" makes it use WebRTC. Since this app is a libpeer/WebRTC client, it
// MUST declare clientType=web, devType=desktop, os=win — exactly like the
// tvOS app. A "true" native client would need a UDP RTP receiver instead;
// out of scope here (see the tvOS project's CLAUDE.md for the full story).
namespace opennow
{

struct BoosteroidControlEvent
{
    enum class Type
    {
        WebrtcEngineReady, // {"type":"settings","action":"webrtc"} - start WebRTC signaling now.
        SessionActive,     // stream/* burst - session is live (take-over/switch fallback trigger).
        Cursor,
        ControllerAck,     // Server echoed our controller "connected" with an assigned numeric id.
        ControllerRumble,
        Raw,
        Closed,
        Failed,
    };

    Type type = Type::Raw;
    std::string raw_type;
    std::string raw_action;
    // Cursor
    bool has_cursor_x = false;
    int cursor_x = 0;
    bool has_cursor_y = false;
    int cursor_y = 0;
    // ControllerAck / ControllerRumble
    std::string controller_name;
    std::string controller_id;
    double rumble_left = 0;
    double rumble_right = 0;
    // Failed
    std::string error_message;
};

class BoosteroidControlChannel
{
  public:
    BoosteroidControlChannel();
    ~BoosteroidControlChannel();

    using EventHandler = std::function<void(const BoosteroidControlEvent&)>;

    // Opens the socket. `queryString` is EXACTLY session/details' `queryString`
    // JWT (SessionInfo::query_string) — not decorative, it's the control
    // socket's auth. Call Poll() regularly from the main loop after this
    // (mirrors WebSocketClient's non-blocking curl-based design).
    bool Connect(
        const std::string& node_base_url,
        const std::string& query_string,
        int resolution_width,
        int resolution_height,
        int refresh_rate,
        int max_bitrate_bps,
        EventHandler on_event,
        const std::string& language = "en",
        const std::string& os = "win",
        const std::string& dev_type = "desktop",
        const std::string& client_type = "web");

    void Disconnect();
    void Poll();
    bool IsOpen() const { return is_open_; }

    // Sends one input/control event: {"type":type,["action":action,]
    // ...fields}. For the four "external device" types (keyboard/mouse/
    // controller/finger — CONFIRMED literal set from SessionHandler.sendEvents)
    // id_cmd/from_udp are appended automatically, matching the real client.
    // `fields` is a raw JSON object body fragment (see the .cpp for the
    // small typed helpers built on top of this for each input kind).
    void SendKeyboardButton(bool is_pressed, int vk_code);
    void SendMouseConnected();
    void SendMouseMoveRelative(int dx, int dy, int surface_width, int surface_height);
    // CONFIRMED 2026-07-27 by live-capturing real outgoing frames: x/y are
    // FRACTIONS (0.0-1.0) of the surface, not pixels, and this shape has NO
    // surfaceWidth/surfaceHeight field at all (only the relative-move shape
    // does).
    void SendMouseMoveAbsolute(double fraction_x, double fraction_y);
    void SendMouseButton(bool is_pressed, int button);
    void SendMouseWheel(int delta_y); // +1 or -1
    void SendControllerConnected(const std::string& name);
    void SendControllerDisconnected(int id);
    void SendControllerButton(int id, int button_index, int value);
    void SendControllerAxis(int id, int axis_index, int value);
    void SendControllerPad(int id, int hat_bitmask);

  private:
    std::unique_ptr<WebSocketClient> ws_;
    bool is_open_ = false;
    int id_cmd_counter_ = 0;
    int status_framerate_ = 60;
    int status_bandwidth_bps_ = 0;
    EventHandler on_event_;

    // `fields` is a borrowed json_t* object (or nullptr) whose key/value
    // pairs are merged into the {type,[action,]...} envelope; ownership stays
    // with the caller. id_cmd/from_udp are appended automatically for the
    // four "external device" types.
    void SendRaw(const std::string& type, const char* action, json_t* fields);
    void HandleMessage(const std::string& text);
    void SendStatusHandshake();
};

} // namespace opennow
