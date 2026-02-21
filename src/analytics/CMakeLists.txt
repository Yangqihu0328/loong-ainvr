# FEAT-4.1: Analytics Aggregation Service

set(ANALYTICS_SOURCES
  analytics_store.cc
  analytics_aggregator.cc
)

add_library(loong_analytics STATIC ${ANALYTICS_SOURCES})

target_include_directories(loong_analytics
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loong_analytics
  PUBLIC
    loong_core
    loong_rules
    SQLite::SQLite3
    spdlog::spdlog
)
