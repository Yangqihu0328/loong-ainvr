# COMP-02 Extension: Video Input (ONVIF + RTSP Client + Stream Manager)

# pugixml as a separate library with relaxed warnings (third-party code)
set(PUGIXML_DIR ${CMAKE_SOURCE_DIR}/third_party/pugixml)

add_library(pugixml_lib STATIC ${PUGIXML_DIR}/pugixml.cpp)
target_include_directories(pugixml_lib SYSTEM PUBLIC ${PUGIXML_DIR})
# Disable strict warnings for third-party code
set_target_properties(pugixml_lib PROPERTIES
  COMPILE_OPTIONS "-w"
)

add_library(loong_video_input STATIC
  onvif/onvif_discovery.cc
  onvif/onvif_device.cc
  onvif/onvif_ptz.cc
  rtsp_client/rtsp_client.cc
  stream_manager/stream_manager.cc
)

target_include_directories(loong_video_input
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(loong_video_input SYSTEM
  PUBLIC
    ${PUGIXML_DIR}
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

target_link_libraries(loong_video_input
  PUBLIC
    loong_core
    pugixml_lib
    spdlog::spdlog
    Threads::Threads
    ${AVFORMAT_LIBRARIES}
    ${AVCODEC_LIBRARIES}
    ${AVUTIL_LIBRARIES}
)
