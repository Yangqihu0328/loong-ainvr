# COMP-03: Pipeline Stages Library

add_library(loong_pipeline_stages STATIC
  input_stage/input_stage.cc
  decode_stage/decode_stage.cc
  ai_stage/ai_stage.cc
  overlay_stage/overlay_stage.cc
  output_stage/output_stage.cc
)

target_include_directories(loong_pipeline_stages
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loong_pipeline_stages
  PUBLIC
    loong_core
    loong_codec
    loong_overlay
    loong_ai_engine
    loong_video_input
    ${FFMPEG_LIBRARIES}
    ${OpenCV_LIBS}
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
