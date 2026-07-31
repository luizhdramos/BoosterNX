cmake_minimum_required(VERSION 3.18)

if (NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO is not set. Install devkitPro and export DEVKITPRO before building.")
endif ()

set(DEVKITPRO $ENV{DEVKITPRO})
set(CMAKE_TOOLCHAIN_FILE ${DEVKITPRO}/cmake/Switch.cmake CACHE FILEPATH "devkitPro Switch toolchain")

project(BoosterNX LANGUAGES C CXX)

set(PLATFORM_SWITCH ON)
# One Deko3D application supports both NVDEC zero-copy and software-decoded
# YUV uploads. Decoder selection is a runtime setting rather than a build type.
# Mirrors SwitchNOW's CMakeLists.txt (see ../SwitchNOW/CMakeLists.txt) — the
# borealis/libpeer/FFmpeg/Deko3D build setup below is entirely reused
# verbatim, since none of it is protocol-specific.
set(USE_GLFW OFF CACHE BOOL "Disable GLFW for unified Deko3D" FORCE)
set(USE_DEKO3D ON CACHE BOOL "Use Deko3D on Switch" FORCE)
set(BOREALIS_USE_DEKO3D ON CACHE BOOL "Use Borealis Deko3D driver" FORCE)
set(USE_GLES2 OFF CACHE BOOL "Disable GLES for unified Deko3D" FORCE)
set(USE_SDL2 OFF)
set(USE_GLES3 OFF CACHE BOOL "Disable OpenGL ES 3 on Switch")
set(BRLS_UNITY_BUILD OFF CACHE BOOL "Disable Borealis unity builds")
set(FMT_OS OFF CACHE BOOL "Disable fmt OS-specific APIs for devkitA64")

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)

add_subdirectory(extern/borealis/library)
add_subdirectory(extern/libpeer)

target_include_directories(borealis PUBLIC ${DEVKITPRO}/portlibs/switch/include)

# Explicit source list (NOT file(GLOB_RECURSE ...)) so app/src/_legacy_gfn_reference/
# — the GFN-specific files this port replaced (gfn_client, NTE auto-login, QR
# device-flow login, providers/catalog tabs, the old datachannel-based
# WebRTC negotiation) — is never compiled. Those files are kept only for
# reference; see BoosterNX's CLAUDE.md for what replaced each one.
set(APP_SOURCES
    app/src/LoginView.cpp
    app/src/StreamView.cpp
    app/src/WebSocketClient.cpp
    app/src/app_paths.cpp
    app/src/app_state.cpp
    app/src/boosteroid_client.cpp
    app/src/boosteroid_control_channel.cpp
    app/src/boosteroid_signaling_client.cpp
    app/src/cover_image_cache.cpp
    app/src/game_card_view.cpp
    app/src/game_detail_view.cpp
    app/src/http_client.cpp
    app/src/library_tab.cpp
    app/src/localization.cpp
    app/src/main.cpp
    app/src/main_activity.cpp
    app/src/main_tabs_view.cpp
    app/src/mbedtls_timing_compat.c
    app/src/network_utils.cpp
    app/src/play_history.cpp
    app/src/runtime_journal.cpp
    app/src/search_tab.cpp
    app/src/settings_tab.cpp
    app/src/status_tab.cpp
    app/src/stream_diagnostics.cpp
    app/src/stream_settings.cpp
    app/src/switch_wrapper_compat.c
    app/src/top_bar_frame.cpp
    app/src/ui_action_guard.cpp
    app/src/ui_helpers.cpp
    app/src/stream/OpenGL/GLVideoRenderer.cpp
    app/src/stream/audio/AudioPipeline.cpp
    app/src/stream/deko3d/DKVideoRenderer.cpp
    app/src/stream/ffmpeg/AVFrameHolder.cpp
    app/src/stream/ffmpeg/FFmpegVideoDecoder.cpp
    app/src/stream/ffmpeg/dav1d_stubs.c
    app/src/webrtc/input.cpp
    app/src/webrtc/log.cpp
    app/src/webrtc/media.cpp
    app/src/webrtc/session.cpp
    app/src/webrtc/shared.cpp
)

set(PLATFORM_SOURCES)

if (PLATFORM_SWITCH)
    list(APPEND PLATFORM_SOURCES
        extern/borealis/library/lib/platforms/switch/switch_wrapper.c
    )
endif ()

add_executable(${PROJECT_NAME} ${APP_SOURCES} ${PLATFORM_SOURCES})

target_include_directories(${PROJECT_NAME}
    PRIVATE
        app/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library/lib/extern/glfw/deps
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg/include
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/libpeer/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/libpeer/third_party/mbedtls/include
)

# Prefer the in-tree libpeer dependencies over portlibs archives with the same
# names. Mixing mbedTLS versions produces TLS failures or an unusable NRO.
target_link_directories(${PROJECT_NAME}
    BEFORE PRIVATE
        ${CMAKE_BINARY_DIR}/dist/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/ffmpeg/lib
)

target_compile_definitions(${PROJECT_NAME} PRIVATE PLATFORM_SWITCH __SWITCH__ HAS_SOCKLEN_T=1)
if (PLATFORM_SWITCH)
    target_compile_definitions(${PROJECT_NAME} PRIVATE USE_DK_RENDERER BOREALIS_USE_DEKO3D OPENNOW_ENABLE_NVDEC)
    find_program(NX_UAM_EXE NAMES uam HINTS "${DEVKITPRO}/tools/bin")
    if (NOT NX_UAM_EXE)
        message(FATAL_ERROR "uam is required for the NVDEC/Deko3D video shaders")
    endif ()
    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/resources/shaders")
    set(BOREALIS_DEKO_SHADER_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/extern/borealis/library/lib/extern/nanovg/deko3d/shaders")
    execute_process(COMMAND ${NX_UAM_EXE} -s vert
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_vsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_vsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_aa_fsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_aa_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/fill_fsh.dksh"
        "${BOREALIS_DEKO_SHADER_DIR}/fill_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s vert
        -o "${CMAKE_BINARY_DIR}/resources/shaders/opennow_video_vsh.dksh"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/stream/deko3d/basic_vsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag
        -o "${CMAKE_BINARY_DIR}/resources/shaders/opennow_video_fsh.dksh"
        "${CMAKE_CURRENT_SOURCE_DIR}/app/src/stream/deko3d/texture_fsh.glsl"
        COMMAND_ERROR_IS_FATAL ANY)
endif ()

target_compile_options(${PROJECT_NAME}
    PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
        -ffunction-sections
        -fdata-sections
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        borealis
        PkgConfig::CURL
        PkgConfig::JANSSON
        peer
        -lavformat
        -lavcodec
        -lavutil
        -lswresample
        z
)

set(PROJECT_AUTHOR "OpenCloudGaming")
set(PROJECT_VERSION_STRING "0.1.0")
# Distinct from SwitchNOW's title ID (05004F4E4F575358) — arbitrary but
# unique, since this is sideload-only homebrew with no real title ID
# registration.
set(PROJECT_TITLEID "05004F4E4F425354")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon/icon.jpg)
set(PROJECT_ROMFS_DIR ${CMAKE_BINARY_DIR}/romfs)

add_custom_target(${PROJECT_NAME}.nro
    DEPENDS ${PROJECT_NAME}
    COMMAND ${NX_NACPTOOL_EXE}
        --create
        "BoosterNX"
        "${PROJECT_AUTHOR}"
        "${PROJECT_VERSION_STRING}"
        ${PROJECT_NAME}.nacp
        --titleid=${PROJECT_TITLEID}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${PROJECT_ROMFS_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_ROMFS_DIR}/img
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/font
        ${PROJECT_ROMFS_DIR}/font
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/i18n
        ${PROJECT_ROMFS_DIR}/i18n
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/icon
        ${PROJECT_ROMFS_DIR}/icon
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/cert
        ${PROJECT_ROMFS_DIR}/cert
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/sys
        ${PROJECT_ROMFS_DIR}/img/sys
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/material
        ${PROJECT_ROMFS_DIR}/material
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_BINARY_DIR}/resources/shaders
        ${PROJECT_ROMFS_DIR}/shaders
    COMMAND ${NX_ELF2NRO_EXE}
        ${PROJECT_NAME}.elf
        ${PROJECT_NAME}.nro
        --icon=${PROJECT_ICON}
        --nacp=${PROJECT_NAME}.nacp
        --romfsdir=${PROJECT_ROMFS_DIR}
)
