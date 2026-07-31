cmake_minimum_required(VERSION 3.16)
cmake_policy(VERSION 3.16)

include(${CMAKE_CURRENT_LIST_DIR}/third_party/coreHTTP/httpFilePaths.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/third_party/coreMQTT/mqttFilePaths.cmake)

if (DEFINED ENV{IDF_PATH})
  file(GLOB ESP32_CODES "./src/*.c")
  idf_component_register(
   SRCS ${ESP32_CODES} ${HTTP_SOURCES} ${MQTT_SOURCES} ${MQTT_SERIALIZER_SOURCES}
   INCLUDE_DIRS "./include" ${HTTP_INCLUDE_PUBLIC_DIRS} ${MQTT_INCLUDE_PUBLIC_DIRS}
   PRIV_INCLUDE_DIRS "./src"
   REQUIRES mbedtls srtp json esp_netif
  )
  add_definitions("-DCONFIG_USE_LWIP=1" "-DCONFIG_USE_USRSCTP=0" "-DCONFIG_AUDIO_BUFFER_SIZE=8096" "-DCONFIG_DATA_BUFFER_SIZE=102400" "-D__BYTE_ORDER=__LITTLE_ENDIAN" "-DHTTP_DO_NOT_USE_CUSTOM_CONFIG" "-DMQTT_DO_NOT_USE_CUSTOM_CONFIG")
  return()
endif()

project(peer)

option(ENABLE_TESTS "Enable tests" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(ADDRESS_SANITIZER "Build with AddressSanitizer." OFF)
option(MEMORY_SANITIZER "Build with MemorySanitizer." OFF)
option(THREAD_SANITIZER "Build with ThreadSanitizer." OFF)
option(UNDEFINED_BEHAVIOR_SANITIZER "Build with UndefinedBehaviorSanitizer." OFF)

include(ExternalProject)

include_directories(${CMAKE_BINARY_DIR}/dist/include ${CMAKE_BINARY_DIR}/dist/include/cjson)

link_directories(${CMAKE_BINARY_DIR}/dist/lib)

set(DEP_LIBS "srtp2" "usrsctp" "mbedtls" "mbedcrypto" "mbedx509" "cjson")
# Extended debug information (symbols, source code, and macro definitions)
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3")

function(enableSanitizer SANITIZER)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g -fsanitize=${SANITIZER} -fno-omit-frame-pointer -fno-optimize-sibling-calls" PARENT_SCOPE)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=${SANITIZER}" PARENT_SCOPE)
endfunction()

if(ADDRESS_SANITIZER)
  enableSanitizer("address")
endif()

if(MEMORY_SANITIZER)
  enableSanitizer("memory")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize-memory-track-origins")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize-memory-track-origins")
endif()

if(THREAD_SANITIZER)
  enableSanitizer("thread")
endif()

if(UNDEFINED_BEHAVIOR_SANITIZER)
  enableSanitizer("undefined")
endif()

add_definitions("-Wunused-variable -Werror=sequence-point -Werror=pointer-sign -Werror=return-type -Werror=sizeof-pointer-memaccess -Wincompatible-pointer-types -DHTTP_DO_NOT_USE_CUSTOM_CONFIG -DMQTT_DO_NOT_USE_CUSTOM_CONFIG")

add_subdirectory(src)
add_subdirectory(examples)

if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()

# CONFIGURE_COMMAND is spelled out explicitly (with -S/-B/-G) for every
# external project below, rather than relying on ExternalProject_Add's
# automatic "<cmake> <SOURCE_DIR> <CMAKE_ARGS>" command construction.
# CONFIRMED broken on this toolchain/CMake combination: the auto-constructed
# configure command silently DROPS the source directory argument entirely,
# producing "CMake Warning: No source or binary directory provided" followed
# by "CMake Error: The source directory '.../<name>-build' does not appear
# to contain CMakeLists.txt" for every one of the four external projects
# (cjson/mbedtls/srtp2/usrsctp alike — this is a generic ExternalProject_Add
# construction bug on this CMake version, not anything specific to one
# project's own CMakeLists.txt). Writing the command out ourselves sidesteps
# whatever internal logic is misfiring. When CONFIGURE_COMMAND is given
# explicitly, ExternalProject_Add's CMAKE_ARGS keyword no longer auto-applies
# — each project's -D flags are folded directly into its CONFIGURE_COMMAND
# list below instead.
set(_PEER_EXTDEP_CMAKE_ARGS
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

ExternalProject_Add(cjson
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/cjson-build
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
    -S ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cJSON
    -B ${CMAKE_CURRENT_BINARY_DIR}/cjson-build
    -G ${CMAKE_GENERATOR}
    ${_PEER_EXTDEP_CMAKE_ARGS}
    -DBUILD_SHARED_LIBS=off
    -DENABLE_CJSON_TEST=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
  # BYPRODUCTS is REQUIRED for the Ninja generator: without it, Ninja has no
  # rule for producing dist/lib/libcjson.a and errors with "missing and no
  # known rule to make it" as soon as src/CMakeLists.txt references that
  # absolute path in target_link_libraries(peer INTERFACE ...). The Makefiles
  # generator doesn't enforce this as strictly, which is presumably why this
  # went unnoticed until a Ninja build was actually attempted.
  BYPRODUCTS ${CMAKE_BINARY_DIR}/dist/lib/libcjson.a
)

ExternalProject_Add(mbedtls
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/mbedtls-build
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
    -S ${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls
    -B ${CMAKE_CURRENT_BINARY_DIR}/mbedtls-build
    -G ${CMAKE_GENERATOR}
    ${_PEER_EXTDEP_CMAKE_ARGS}
    -DENABLE_TESTING=off
    -DENABLE_PROGRAMS=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
    # This vendored mbedTLS checkout doesn't include the pre-generated
    # library/error.c / ssl_debug_helpers_generated.c (those ship in mbedTLS's
    # official release tarballs but not a plain source checkout), and
    # GEN_FILES defaults OFF, assuming they're already present. Turning it on
    # runs mbedTLS's own scripts/generate_errors.pl and
    # generate_ssl_debug_helpers.py to produce them (needs Python3 + Perl on
    # the host machine doing the build, not the Switch target).
    -DGEN_FILES=ON
  BYPRODUCTS
    ${CMAKE_BINARY_DIR}/dist/lib/libmbedtls.a
    ${CMAKE_BINARY_DIR}/dist/lib/libmbedcrypto.a
    ${CMAKE_BINARY_DIR}/dist/lib/libmbedx509.a
)
# Apply Switch overrides once. Re-appending them on every configure grows the
# source file indefinitely and eventually makes repeated builds unreliable.
set(_SWITCHNOW_MBEDTLS_CONFIG
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mbedtls/include/mbedtls/mbedtls_config.h")
set(_SWITCHNOW_MBEDTLS_MARKER "/* SwitchNOW mbedTLS overrides */")
file(READ "${_SWITCHNOW_MBEDTLS_CONFIG}" INPUT_CONTENT)
if(NOT INPUT_CONTENT MATCHES "Switch mbed[Tt][Ll][Ss] overrides")
  string(REPLACE
    "//#define MBEDTLS_SSL_DTLS_SRTP"
    "#define MBEDTLS_SSL_DTLS_SRTP"
    MODIFIED_CONTENT
    "${INPUT_CONTENT}")
  file(WRITE
    "${_SWITCHNOW_MBEDTLS_CONFIG}"
    "${MODIFIED_CONTENT}\n${_SWITCHNOW_MBEDTLS_MARKER}\n#define MBEDTLS_NO_PLATFORM_ENTROPY\n#define MBEDTLS_TEST_NULL_ENTROPY\n#undef MBEDTLS_TIMING_C\n#undef MBEDTLS_NET_C\n")
endif()

ExternalProject_Add(srtp2
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsrtp
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/srtp2-build
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
    -S ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libsrtp
    -B ${CMAKE_CURRENT_BINARY_DIR}/srtp2-build
    -G ${CMAKE_GENERATOR}
    ${_PEER_EXTDEP_CMAKE_ARGS}
    -DTEST_APPS=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
  BYPRODUCTS ${CMAKE_BINARY_DIR}/dist/lib/libsrtp2.a
)

ExternalProject_Add(usrsctp
  SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/usrsctp
  BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/usrsctp-build
  CONFIGURE_COMMAND ${CMAKE_COMMAND}
    -S ${CMAKE_CURRENT_SOURCE_DIR}/third_party/usrsctp
    -B ${CMAKE_CURRENT_BINARY_DIR}/usrsctp-build
    -G ${CMAKE_GENERATOR}
    ${_PEER_EXTDEP_CMAKE_ARGS}
    -Dsctp_build_programs=off
    -Dsctp_inet6=0
    # usrsctp defaults sctp_werror ON (-Werror). GCC 16 (this toolchain) flags
    # a variable in sctp_usrreq.c ("'cnt' set but not used") that older GCC
    # didn't, turning a harmless warning into a hard build failure. Disabling
    # -Werror for this vendored copy is safer than patching upstream C source
    # we didn't write and don't want to carry a diff against.
    -Dsctp_werror=off
    -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/dist
  BYPRODUCTS ${CMAKE_BINARY_DIR}/dist/lib/libusrsctp.a
)
