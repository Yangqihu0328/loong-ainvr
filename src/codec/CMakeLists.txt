# COMP-02: Codec Library (FFmpeg Decoder/Encoder)

add_library(loong_codec STATIC
  decoder/ffmpeg_decoder.cc
  encoder/ffmpeg_encoder.cc
  codec_factory/codec_factory.cc
)

target_include_directories(loong_codec
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loong_codec
  PUBLIC
    loong_core
    ${FFMPEG_LIBRARIES}
    spdlog::spdlog
)
