# COMP-05: Video Overlay Library (OSD Renderer + FreeType Text + Compositor)

find_package(Freetype REQUIRED)

add_library(loong_overlay STATIC
  osd_renderer/osd_renderer.cc
  ft_text_renderer/ft_text_renderer.cc
  compositor/channel_compositor.cc
)

target_include_directories(loong_overlay
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
  PRIVATE
    ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(loong_overlay
  PUBLIC
    loong_core
    ${OpenCV_LIBS}
    spdlog::spdlog
  PRIVATE
    ${FREETYPE_LIBRARIES}
)
