# CLAUDE.md

This file provides guidance to Claude when working with code in this repository.

## Project

**BoosterNX** is a native Nintendo Switch homebrew client for
[Boosteroid](https://cloud.boosteroid.com) cloud gaming. It is a sibling
project to two others in this workspace:

- **SwitchNOW** (`../SwitchNOW`) — a GeForce NOW client for Switch. This
  project reuses SwitchNOW's entire protocol-agnostic foundation verbatim:
  borealis (UI), libpeer (WebRTC), FFmpeg (decode), Deko3D/OpenGL
  (rendering), and the RTP/decode-queue pipeline. Only the parts that talk
  to the cloud-gaming provider were rewritten.
- **BoosteroidATV** (`../BoosteroidATV`) — a native tvOS Boosteroid client
  whose Swift source is the **confirmed protocol reference** this project
  was ported from. Its `CLAUDE.md` documents the full confirmation trail
  (dates, capture method) for every endpoint and message shape used here;
  when in doubt about *why* this project's C++ does something a particular
  way, check the equivalent Swift file over there first.

## Status: builds and runs on real hardware; session lifecycle CONFIRMED end-to-end, WebRTC media still fails

Originally scaffolded without a devkitA64 environment available (see "What's
ported" below, written at that time — still accurate background), then
actually built and iterated against real hardware starting 2026-07-31 (this
project was renamed from BoosteroidSwitch to BoosterNX partway through that
pass; older commit messages/branches may still say BoosteroidSwitch).
CONFIRMED on real hardware as of 2026-07-31:

- **Build**: compiles clean under devkitA64 (Unix Makefiles generator —
  see `scripts/build-switch-msys2.sh`'s header comment for why not Ninja)
  once vendored cjson/mbedtls/srtp2/usrsctp `ExternalProject_Add` blocks in
  `extern/libpeer/CMakeLists.txt` got explicit `CONFIGURE_COMMAND`/
  `BYPRODUCTS`, mbedtls got `-DGEN_FILES=ON`, and usrsctp got
  `-Dsctp_werror=off` (GCC 16 flags a warning the vendored source predates).
- **HTTPS**: devkitPro's switch-curl reports its SSL backend as literally
  `"libnx"` (not a real curl backend — some devkitPro-maintained shim) and
  does NOT honor a bundled PEM `CURLOPT_CAINFO` the normal way — strict
  verification against `resources/cert/cacert.pem` is tried first, and
  `http_client.cpp`'s `Request()`/`MeasureConnectLatencyMs()` fall back to
  `CURLOPT_SSL_VERIFYPEER=0` on a CA-related curl error, logging
  `ssl_verify_bypassed` when that happens (CONFIRMED this fallback fires on
  every single HTTPS call on real hardware — the strict path never
  succeeds).
- **Login → catalog → queue → session confirmation**: CONFIRMED working
  end-to-end, including a real queued (not instant-"LI") session. The
  original scaffold never wired anything to `SetPreferredSessionId`/
  `StartStreamingSession` (the queue's v2 confirmation) at all — fixed by
  connecting the realtime queue-position WebSocket
  (`wss://cloud.boosteroid.com/ws?uid=<numeric id>&token=<raw access
  token>`) directly inside `BoosteroidClient::CreateAndAwaitSession` and
  reacting to its `{"type":"queues","action":"start","value":{"appId",
  "token"}}` push — CONFIRMED exact shape by live capture 2026-07-31 (see
  `boosteroid_client.cpp`'s `realtime_message`/`realtime_queue_*`
  `LogRuntimeEvent` calls, written specifically so a `runtime.log` pull off
  the SD card shows this without needing extended diagnostics).
- **WebRTC signaling gets partway then fails**: `getIceServers`/`getParams`/
  `call` all succeed (200, real SDP answer received), but both
  `addIceCandidate` POSTs come back **HTTP 500**, and the connection never
  reports any received packets (`state=peer failed packed=0 frames=0`) —
  the current open bug. Two findings from the 2026-07-31 pass:
  - **A real bug was found and fixed in the remote-ICE poll loop** (leading
    candidate for the actual root cause, not yet re-tested on hardware):
    `getIceCandidate` is a POLL that returns the node's ENTIRE current
    candidate list on every call (CONFIRMED: byte-identical 571-byte body
    every second), but `start_ice_poll_worker()` fed every response straight
    into `peer_connection_add_ice_candidate()` with no dedupe — and libpeer
    doesn't dedupe either. Its remote table is a fixed **10** entries
    (`AGENT_MAX_CANDIDATES`, extern/libpeer/src/agent.h) and the pair table
    **100** (`AGENT_MAX_CANDIDATE_PAIRS`), so within a few seconds both
    overflowed with duplicates of the same handful of candidates
    ("Remote ICE candidate table is full") while connectivity checks
    thrashed across duplicate pairs. Fixed with `added_remote_ice_`
    (webrtc_session.hpp).
  - `addIceCandidate`'s 500 may still be a separate (possibly harmless)
    issue — its request body shape is UNCONFIRMED, assumed from the upstream
    webrtc-streamer OSS convention. It's fire-and-forget by design (matching
    the tvOS client), and ICE can still converge without it since the server
    can learn our address as a peer-reflexive candidate from our own binding
    requests. The response body is now logged rather than discarded.
- **Diagnostics are deliberately in `runtime.log`, not just the trace log**:
  the flight recorders (`stream_trace.log`/`signaling.log`/`input.log`, see
  `webrtc/log.cpp`) are all gated behind `StreamDiagnosticsEnabled()`, which
  until 2026-07-31 was ONLY applied at boot (`main.cpp`) — toggling
  Settings → Stream → "Debug overlay" and saving did nothing until a full
  app restart, so the expected trace file simply never appeared (CONFIRMED
  on real hardware; fixed by also applying it in `SettingsTab::SaveChanges`).
  Because of that trap, the WebRTC facts that matter for this bug — ICE
  server list, local-offer/remote-answer candidate type counts
  (`host=/srflx=/relay=`), every remote candidate added, `addIceCandidate`
  failures, and peer state transitions — are logged via `LogRuntimeEvent`,
  which is always on and needs no setting.

Treat the file-by-file notes below as still-accurate background on what was
ported and from where, but not as the current build/run status.

### What's ported and how confident each piece is

- **Auth** (`boosteroid_client.hpp/cpp`): direct email/password login via
  `POST /api/v1/auth/login`, mirroring `BoosteroidClient.login` /
  `BoosteroidAuthAPI.swift` in BoosteroidATV — CONFIRMED against real
  Android-TV-app traffic capture (see that Swift file's header comment for
  the capture method and date). This is simpler than GFN's device-flow
  QR+PIN login (`LoginView.hpp/cpp` is two text fields and a submit button,
  no external browser step). Cookie-session auth (Laravel/Sanctum-style) is
  used for every other REST call — see `BuildCookieHeader`/`CookieJar` in
  `boosteroid_client.hpp`.
- **Catalog** (`boosteroid_client.cpp`'s `FetchLibrary`): CONFIRMED
  `GET /api/v1/boostore/applications/installed`. Deliberately much smaller
  than SwitchNOW's `GameInfo`/`PublicGame` — Boosteroid has no CONFIRMED
  public/store catalog endpoint, only the signed-in user's own library (see
  `models.hpp`'s header comment). `SearchTab` and `LibraryTab`'s "search"
  both therefore only filter the local library, unlike SwitchNOW's
  library+store combined search.
- **Session lifecycle** (`boosteroid_client.cpp`'s `CreateSession`/
  `CreateAndAwaitSession`/`FetchSessionDetails`/`StartStreamingSession`):
  ported from `BoosteroidClient.swift`'s CONFIRMED end-to-end flow —
  enqueue → poll `last-session` → (once reserved) confirm via
  `session/start` v2 → poll `session/details` for `gw`+`queryString`. The
  RATE LIMITING constraints documented in BoosteroidATV's CLAUDE.md
  (never poll `session/start` on a timer; slow 60s polling while queued)
  are carried over into `CreateAndAwaitSession`'s default intervals —
  **do not lower them** without re-reading that section first.
- **WebRTC signaling** (`boosteroid_signaling_client.hpp/cpp`): REST-based,
  matching the confirmed webrtc-streamer-shaped API
  (`getIceServers`/`getParams`/`call`/`addIceCandidate`/`getIceCandidate`
  polling). **This client is the WebRTC offerer** (unlike GFN, where the
  server offers) — `webrtc/session.cpp`'s `start_webrtc_media()` creates the
  offer itself.
- **Control WebSocket** (`boosteroid_control_channel.hpp/cpp`): CONFIRMED to
  be the PRIMARY connection in BoosteroidATV's investigation — it claims the
  session (switches device away from any other client streaming it), gates
  when WebRTC signaling may start (`settings/webrtc` push), and carries ALL
  input as JSON text frames instead of a WebRTC datachannel. `clientType=web`
  is REQUIRED (a `native`/`tv` clientType makes the server switch to a raw-UDP
  transport this app does not implement — see that file's header comment).
- **Input** (`webrtc/input.cpp`, `StreamView.cpp`'s `PollGamepads`):
  controller button/axis/D-pad indices are CONFIRMED (mirrors
  `InputSender.swift`'s `pollGamepad`). **Physical USB/Bluetooth keyboard and
  mouse input is NOT wired up** — controller input only, a deliberate
  first-pass scope cut (see `StreamView.hpp`'s header comment). Only one
  merged local controller is polled (`padInitializeDefault`) — true
  per-player multi-controller support (`hidGetNpadStyleSet` + one `PadState`
  per player) is future work.
- **Session end**: no CONFIRMED teardown REST/WS call exists yet in either
  this project or BoosteroidATV — `StreamView::ExitStream` just stops the
  local `WebRtcSession`/control channel and relies on the session's own
  server-side inactivity timeout. See BoosteroidATV's CLAUDE.md "Session
  end" section for what's been ruled out so far.

### What's simplified relative to SwitchNOW (and why)

Boosteroid's confirmed API surface is genuinely smaller than GFN's, so a
faithful port is smaller too, not just a first-pass cut corner:

- **No server-side region/gateway choice** — `session/details` (or the v2
  `session/start` 201 body) assigns the gateway; there is nothing for
  `SettingsTab` to let the user pick, unlike SwitchNOW's per-region server
  list.
- **No refresh-token flow** — `AuthTokens` has no CONFIRMED refresh
  mechanism (see `models.hpp`). `MainActivity`'s `StartupGateView` does a
  best-effort `GET /api/v1/user` validation of saved cookies and always
  proceeds to `MainTabsView` either way; a genuinely dead session surfaces
  the first time a tab makes an authenticated call (`LibraryTab::ReloadLibrary`
  shows the error inline) rather than through a background reauth loop like
  SwitchNOW's `MainTabsView::MaybeRefreshAuthentication`.
- **`SettingsTab` has 5 categories, not 7** — Account, Stream, Controls,
  Interface, Storage. No "Game" category (GFN's per-session game-language
  parameter has no Boosteroid equivalent) and no dedicated "Audio" category
  (the audio pipeline itself, `stream/audio/AudioPipeline.*`, is reused
  as-is; there just isn't a Boosteroid-specific audio setting to expose yet
  beyond what `StreamSettings` already has).
- **`GameDetailView`/`GameDetailData`** dropped the store-variant picker and
  screenshot carousel entirely: Boosteroid's confirmed library payload is
  `id`/`name`/`icon`/`bannerImage`/`installed` only (see `models.hpp`'s
  `GameInfo`) — there's no description, genre, or screenshots data to show.

### Interface language persistence — a bug fixed during this pass

`localization.cpp`'s `SetInterfaceLanguage`/`GetInterfaceLanguage` only ever
held an in-memory `g_language` global with no persistence — copied as-is
from SwitchNOW, this would have silently reset to English on every relaunch
even though `SettingsTab` (and `main.cpp`, which calls
`SetInterfaceLanguage(startup_settings.interface_language)` at boot) both
assumed it persisted. Fixed by adding `StreamSettings::interface_language`
(`models.hpp`) and wiring it through `stream_settings.cpp`'s load/save and
`SettingsTab::ChooseInterfaceLanguage`/`RevertChanges`. Worth double-checking
after a first real build that `main.cpp`'s boot-time call actually takes
effect before any UI renders.

### `_legacy_gfn_reference/`

Every GFN-specific file this port replaced — `gfn_client.hpp/cpp`, the
`gfn/*.inc` fragments, `qr_login_dialog.*`, `nte_credentials.*`/
`nte_autologin_log.*` (the "Neverness to Everness" auto-login hack),
`providers_tab.*`, `catalog_tab.*`, `avatar_utils.*`, `image_url_policy.*`,
`native_auth_policy.hpp`, `server_location_policy.hpp`,
`cloud_session_policy.hpp`, `auth_policy.hpp`, the old datachannel-based
`webrtc_session.hpp`/`webrtc/negotiation.cpp`/`webrtc/input.cpp`,
`webrtc_diagnostics.*`/`webrtc_IceDiagnostics.*`, `qrcodegen.*`,
`membership_label.hpp` — is kept under `app/src/_legacy_gfn_reference/` for
reference (e.g. if Boosteroid ever gets a confirmed public catalog and
`SearchTab` should grow a store-search feature analogous to GFN's). It is
**excluded from the CMake build** via an explicit source list rather than
`file(GLOB_RECURSE ...)` (see `CMakeLists.txt`) — do not switch back to a
glob without either deleting this folder or adding it to an exclude list,
or the build will try to compile files that reference removed types
(`GfnClient`, `PublicGame`, provider/store models) and fail.

(Files in `_legacy_gfn_reference/` could not actually be *deleted* while
building this scaffold — the mounted filesystem refused `rm` on
bash-created files even with normal owner/rw permissions, though `mv`
worked fine. Worth trying `rm` again in a normal local checkout; it may be
specific to the sandboxed environment this was built in.)

## Building

Same toolchain as SwitchNOW. **CONFIRMED building on real macOS/Apple
Silicon + devkitA64 (Rosetta 2) as of 2026-07-31** — use
`scripts/build-switch-msys2.sh`, not a bare `cmake -B build -G Ninja`: the
Ninja generator hits a real "multiple rules generate" bug on this project's
nested `ExternalProject_Add` sub-builds (see the script's own header
comment), and the generic instructions below predate that finding.

- **devkitPro/devkitA64**, with `DEVKITPRO` exported.
- `bash scripts/build-switch-msys2.sh` — wraps `cmake -B build/switch -G
  "Unix Makefiles"` + `cmake --build`. `CMakeLists.txt` requires `DEVKITPRO`
  to be set and uses `${DEVKITPRO}/cmake/Switch.cmake` as the toolchain file.
- Build target `BoosterNX.nro` for a sideloadable homebrew package.
- Distribution is sideload-only (no eShop/CFW-store target), same as
  SwitchNOW.
- `resources/i18n/` was copied from SwitchNOW's borealis i18n data (its own
  built-in UI strings for scroll hints etc., NOT this app's own translated
  strings, which live inline in `localization.cpp`'s dictionaries). The copy
  left a stray nested `resources/i18n/i18n/` directory behind (the
  environment's `rm` restriction — see above — prevented cleaning it up
  properly); it's harmless (just a few unused extra KB in the romfs) but
  worth deleting in a normal checkout.
- `resources/icon/icon.jpg` is a generated 256x256 BoosterNX placeholder
  (dark card, streaming-signal glyph, "BoosterNX" wordmark) — not SwitchNOW's
  icon anymore (that was swapped out, then this file regenerated again for
  the BoosteroidSwitch → BoosterNX rename on 2026-07-31). Still a
  placeholder in the sense that it's programmatically generated, not
  hand-designed — swap it for real artwork before a public release if
  wanted.

## Architecture

Mirrors SwitchNOW's five functional areas, Boosteroid-specific pieces noted:

- **Auth**: `boosteroid_client.hpp/cpp` (login, session persistence) +
  `LoginView.hpp/cpp` (email/password UI, new — no in-app browser needed,
  unlike GFN's device-flow) + `models.hpp` (`AuthTokens`/`AuthUser`/
  `AuthSession`, all CONFIRMED shapes).
- **Session**: `boosteroid_client.hpp/cpp` (catalog + full session
  lifecycle) + `models.hpp` (`GameInfo`/`SessionInfo`/`SessionCreateRequest`).
  No `boosteroid_realtime_client` (the cosmetic queue-position WebSocket
  BoosteroidATV implements) — `CreateAndAwaitSession`'s REST polling of
  `last-session`/`session/details` is the authoritative signal and was
  judged sufficient for a first pass; port `BoosteroidRealtimeClient.swift`
  over later if a live queue-position number becomes worth showing.
- **Streaming**: `webrtc_session.hpp` + `webrtc/session.cpp` (opens
  `BoosteroidControlChannel` FIRST, waits for `settings/webrtc`, then runs
  the client-is-offerer WebRTC flow) + `webrtc/media.cpp` (decode queue,
  RTP/keyframe handling — reused near-verbatim from SwitchNOW, genuinely
  protocol-agnostic) + `webrtc/input.cpp` (controller → control-channel JSON
  frames) + `boosteroid_signaling_client.hpp/cpp` (REST signaling) +
  `boosteroid_control_channel.hpp/cpp` (the control WebSocket).
- **Video**: `stream/` — FFmpeg decode, Deko3D/OpenGL rendering, the
  `AVFrameHolder` frame queue. Entirely reused from SwitchNOW; protocol
  never enters this layer (it only ever sees decoded H.264 access units).
- **UI (borealis)**: `main.cpp` (root) → `main_activity.cpp`
  (`StartupGateView`, simplified — see above) → `main_tabs_view.hpp/cpp`
  (`TopBarFrame` subclass with 4 tabs: Library, Search, Settings, Status) →
  `library_tab.*`, `search_tab.*`, `settings_tab.*`, `status_tab.*`,
  `game_detail_view.*`, `LoginView.*`, `StreamView.*`. `top_bar_frame.cpp`
  is reused from SwitchNOW with two targeted fixes: the hardcoded "SwitchNOW"
  brand label and a reference to a `StreamSettings::label` field and
  `ResolveAvatarUrl()` helper that don't exist in this project's simplified
  `models.hpp`/`_legacy_gfn_reference` split (replaced with a computed
  resolution/fps label and `AuthUser::avatar_url` directly).

## Key Patterns

Same as SwitchNOW/BoosteroidATV: borealis's retained-mode view tree,
`brls::async`/`brls::sync` for background network work bridging to the UI
thread, `std::shared_ptr<std::atomic_bool> alive_` guards on any view that
captures `this` in an async lambda (so a closed view doesn't get touched
after destruction) — used throughout `LoginView`, `LibraryTab`,
`SettingsTab`, `StreamView`, `StartupGateView`.

## Verification performed (and NOT performed)

Since this couldn't be compiled, verification here was static:

- Every `#include "local_header.hpp"` across the non-legacy tree resolves to
  a real file (checked with a script — see git history/session notes if you
  need to re-run something similar).
- Every method declared in each rewritten header (`boosteroid_client.hpp`,
  `boosteroid_signaling_client.hpp`, `boosteroid_control_channel.hpp`,
  `webrtc_session.hpp`, `StreamView.hpp`, `LibraryTab`/`SearchTab`/
  `SettingsTab`/`GameDetailView` headers) has a matching definition in its
  `.cpp`.
- Grepped for leftover references to removed GFN-era types (`GfnClient`,
  `PublicGame`, `LoginProvider`, `available_stores`, `membership_tier_label`,
  `ReauthenticationRequired`, etc.) outside `_legacy_gfn_reference/` — none
  found as of this pass.
- Two real bugs were caught and fixed this way: `top_bar_frame.cpp`
  referencing `avatar_utils.hpp`/`ResolveAvatarUrl`/`StreamSettings::label`
  (none of which exist in this project), and `main.cpp`/`SettingsTab`
  assuming `StreamSettings::interface_language` existed when it didn't (see
  "Interface language persistence" above).

**What static checking cannot catch**: actual template/overload resolution
errors, borealis API drift between what SwitchNOW's `extern/borealis` pin
supports and what this code assumes, and any device-only failure (Deko3D
shader compilation, actual libpeer/FFmpeg linking). Budget time for a real
build-and-fix pass with devkitA64 before treating this as more than a
scaffold. Search for `TODO(port):` and `TODO(protocol):` across the tree for
what's known-incomplete going in.
