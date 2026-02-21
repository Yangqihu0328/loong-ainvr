# Unit Tests

include(GoogleTest)

# Core tests
add_executable(core_tests
  unit/core/thread_pool_test.cc
  unit/core/event_bus_test.cc
  unit/core/frame_buffer_pool_test.cc
  unit/core/config_manager_test.cc
  unit/core/config_watcher_test.cc
  unit/core/stage_queue_test.cc
  unit/core/channel_pipeline_test.cc
)

target_link_libraries(core_tests
  PRIVATE
    loong_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(core_tests)

# Channel tests
add_executable(channel_tests
  unit/channel/channel_manager_test.cc
)

target_link_libraries(channel_tests
  PRIVATE
    loong_channel
    loong_network
    loong_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(channel_tests)

# AI Engine tests
add_executable(ai_engine_tests
  unit/ai_engine/yolo_adapter_test.cc
  unit/ai_engine/backend_factory_test.cc
  unit/ai_engine/model_cascade_test.cc
  unit/ai_engine/lpr_test.cc
  unit/ai_engine/face_test.cc
  unit/ai_engine/plugin_test.cc
)

target_link_libraries(ai_engine_tests
  PRIVATE
    loong_ai_engine
    loong_core
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(ai_engine_tests)

# Storage tests
add_executable(storage_tests
  unit/storage/record_index_test.cc
  unit/storage/record_writer_thread_test.cc
  unit/storage/cloud_backup_test.cc
)

target_link_libraries(storage_tests
  PRIVATE
    loong_storage
    loong_core
    SQLite::SQLite3
    ${FFMPEG_LIBRARIES}
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(storage_tests)

# System (Auth + Monitor + Notification) tests
add_executable(system_tests
  unit/system/auth_test.cc
  unit/system/system_monitor_test.cc
  unit/system/notification_test.cc
  unit/system/mqtt_test.cc
)

target_link_libraries(system_tests
  PRIVATE
    loong_system
    loong_core
    SQLite::SQLite3
    OpenSSL::Crypto
    OpenSSL::SSL
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(system_tests)

# Network (WebSocket + RTSP + MediaStream + HLS) tests
add_executable(network_tests
  unit/network/websocket_test.cc
  unit/network/rtsp_server_test.cc
  unit/network/media_stream_test.cc
  unit/network/hls_test.cc
  unit/network/webrtc_service_test.cc
)

target_link_libraries(network_tests
  PRIVATE
    loong_network
    loong_core
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(network_tests)

# Video Input (ONVIF) tests
add_executable(video_input_tests
  unit/video_input/onvif_test.cc
)

target_link_libraries(video_input_tests
  PRIVATE
    loong_video_input
    loong_core
    spdlog::spdlog
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(video_input_tests)

# Pipeline stages (Overlay + Compositor) tests
add_executable(overlay_tests
  unit/pipeline_stages/overlay_test.cc
)

target_link_libraries(overlay_tests
  PRIVATE
    loong_pipeline_stages
    loong_overlay
    loong_core
    loong_codec
    ${OpenCV_LIBS}
    ${FFMPEG_LIBRARIES}
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(overlay_tests)

# Codec (Decoder/Encoder/Factory) tests
add_executable(codec_tests
  unit/codec/codec_test.cc
)

target_link_libraries(codec_tests
  PRIVATE
    loong_codec
    loong_core
    ${FFMPEG_LIBRARIES}
    spdlog::spdlog
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(codec_tests)

# Rules Engine tests
add_executable(rules_tests
  unit/rules/rules_test.cc
)

target_link_libraries(rules_tests
  PRIVATE
    loong_rules
    loong_core
    SQLite::SQLite3
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(rules_tests)

# Analytics tests
add_executable(analytics_tests
  unit/analytics/analytics_test.cc
)

target_link_libraries(analytics_tests
  PRIVATE
    loong_analytics
    loong_rules
    loong_core
    SQLite::SQLite3
    spdlog::spdlog
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(analytics_tests)

# Integration tests (End-to-End pipeline)
add_executable(integration_tests
  integration/pipeline_integration_test.cc
)

target_link_libraries(integration_tests
  PRIVATE
    loong_core
    loong_ai_engine
    loong_pipeline_stages
    loong_storage
    loong_codec
    ${OpenCV_LIBS}
    ${FFMPEG_LIBRARIES}
    SQLite::SQLite3
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    GTest::gtest
    GTest::gtest_main
    Threads::Threads
)

gtest_discover_tests(integration_tests)
