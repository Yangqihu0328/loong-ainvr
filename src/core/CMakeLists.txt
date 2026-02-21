# COMP-01: Core Framework Library

add_library(loong_core STATIC
  logger/logger.cc
  config/config_manager.cc
  config/config_watcher.cc
  config/hot_reload_manager.cc
  event_bus/event_bus.cc
  thread_pool/thread_pool.cc
  memory_pool/frame_buffer_pool.cc
  pipeline/stage_queue.cc
  pipeline/channel_pipeline.cc
)

target_include_directories(loong_core
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loong_core
  PUBLIC
    Threads::Threads
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
