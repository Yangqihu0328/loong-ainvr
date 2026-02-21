# COMP-06: Storage Management Library

add_library(loong_storage STATIC
  record_writer/record_writer.cc
  record_index/record_index.cc
  indexer/indexer.cc
  recorder/recorder.cc
  playback/playback.cc
  storage_cleaner/storage_cleaner.cc
  cloud_backup/cloud_backup_service.cc
  cloud_backup/s3_backend.cc
)

target_include_directories(loong_storage
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loong_storage
  PUBLIC
    loong_core
    ${FFMPEG_LIBRARIES}
    SQLite::SQLite3
    spdlog::spdlog
)
