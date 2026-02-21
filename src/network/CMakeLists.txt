# COMP-07: Network Communication Library

add_library(loong_network STATIC
  http_server/http_server.cc
  http_api/api_routes.cc
  flv_stream/http_flv_service.cc
  flv_stream/flv_muxer.cc
  hls_stream/ts_muxer.cc
  hls_stream/hls_service.cc
  media_stream/fmp4_muxer.cc
  media_stream/ws_media_service.cc
  websocket/websocket_server.cc
  websocket/websocket_frame.cc
  rtsp_server/rtsp_server.cc
  rtsp_server/rtp_packetizer.cc
  webrtc/webrtc_service.cc
)

# LoongRTC WebRTC library (external)
set(LOONGRTC_ROOT "${CMAKE_SOURCE_DIR}/../loong-rtc" CACHE PATH "Path to loong-rtc source")
set(LOONGRTC_BUILD "${LOONGRTC_ROOT}/build" CACHE PATH "Path to loong-rtc build")
set(LOONGRTC_THIRD_PARTY "${LOONGRTC_ROOT}/third_party/x86_64" CACHE PATH "loong-rtc third party")

target_include_directories(loong_network
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(loong_network SYSTEM
  PUBLIC
    ${HTTPLIB_INCLUDE_DIR}
    ${LOONGRTC_ROOT}/include
)

# loong-rtc C headers use patterns incompatible with strict C++ mode
set_source_files_properties(webrtc/webrtc_service.cc
  PROPERTIES COMPILE_FLAGS "-fpermissive -Wno-error"
)

target_compile_definitions(loong_network
  PUBLIC
    CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(loong_network
  PUBLIC
    loong_core
    loong_channel
    loong_storage
    loong_system
    loong_video_input
    loong_rules
    loong_analytics
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
    OpenSSL::SSL
    Threads::Threads
    ${LOONGRTC_BUILD}/libloongRtcClient.so
    ${LOONGRTC_THIRD_PARTY}/lib/libsrtp2.a
    ${LOONGRTC_THIRD_PARTY}/lib/libusrsctp.a
    ${LOONGRTC_THIRD_PARTY}/lib/libwebsockets.a
)
