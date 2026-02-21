# FEAT-1.1: Rules Engine Library

add_library(loong_rules STATIC
  evaluators/rule_evaluator.cc
  evaluators/iou_tracker.cc
  evaluators/cross_line_evaluator.cc
  evaluators/region_intrusion_evaluator.cc
  evaluators/counting_evaluator.cc
  evaluators/loitering_evaluator.cc
  rule_store/rule_store.cc
  rule_engine/rule_engine.cc
  rule_stage/rule_stage.cc
)

target_include_directories(loong_rules
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loong_rules
  PUBLIC
    loong_core
    SQLite::SQLite3
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
