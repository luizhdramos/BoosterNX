project(lib)

file(GLOB SRCS "*.c")

file(GLOB HEADERS "peer.h" "peer_connection.h" "peer_signaling.h")

add_library(peer
  ${SRCS}
  ${HTTP_SOURCES}
  ${MQTT_SOURCES}
  ${MQTT_SERIALIZER_SOURCES}
)

if(PLATFORM_SWITCH)
  target_compile_definitions(peer PRIVATE CONFIG_USE_USRSCTP=0)
endif()

include_directories(peer PUBLIC
  ${HTTP_INCLUDE_PUBLIC_DIRS}
  ${MQTT_INCLUDE_PUBLIC_DIRS}
)

add_dependencies(peer cjson mbedtls usrsctp srtp2)

# Absolute paths prevent the Switch toolchain's earlier portlibs search path
# from resolving incompatible mbedTLS archives.
set(_SWITCHNOW_PEER_DIST_LIBDIR "${CMAKE_BINARY_DIR}/dist/lib")
set(_SWITCHNOW_PEER_ABS_LIBS
  "${_SWITCHNOW_PEER_DIST_LIBDIR}/libsrtp2.a"
  "${_SWITCHNOW_PEER_DIST_LIBDIR}/libusrsctp.a"
  "${_SWITCHNOW_PEER_DIST_LIBDIR}/libmbedtls.a"
  "${_SWITCHNOW_PEER_DIST_LIBDIR}/libmbedx509.a"
  "${_SWITCHNOW_PEER_DIST_LIBDIR}/libmbedcrypto.a"
  "${_SWITCHNOW_PEER_DIST_LIBDIR}/libcjson.a"
)
target_link_libraries(peer INTERFACE ${_SWITCHNOW_PEER_ABS_LIBS})
target_link_directories(peer INTERFACE ${_SWITCHNOW_PEER_DIST_LIBDIR})

set_target_properties(peer PROPERTIES PUBLIC_HEADER "${HEADERS}")

install(TARGETS peer
  LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_PREFIX}/include/
)

