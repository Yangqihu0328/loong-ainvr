# COMP-08: Channel Management Library

add_library(loong_channel STATIC
  channel_manager/channel_manager.cc
  channel_monitor/channel_monitor.cc
  channel_pipeline/channel_orchestrator.cc
  channel_store/channel_store.cc
)

target_include_directories(loong_channel
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(loong_channel SYSTEM
  PRIVATE
    ${HTTPLIB_INCLUDE_DIR}
)

target_link_libraries(loong_channel
  PUBLIC
    loong_core
    loong_pipeline_stages
    loong_ai_engine
    SQLite::SQLite3
)
