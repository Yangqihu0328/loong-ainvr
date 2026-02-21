# COMP-10: System Management Library (Auth + User Management + Monitor + Alarm + Notification)

add_library(loong_system STATIC
  auth/user_store.cc
  auth/jwt_helper.cc
  auth/auth_middleware.cc
  monitor/system_monitor.cc
  alarm/alarm_manager.cc
  notification/notification_manager.cc
  notification/smtp_channel.cc
  notification/webhook_channel.cc
  notification/mqtt_channel.cc
)

target_include_directories(loong_system
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)
target_include_directories(loong_system SYSTEM
  PUBLIC
    ${HTTPLIB_INCLUDE_DIR}
)

target_compile_definitions(loong_system
  PUBLIC
    CPPHTTPLIB_OPENSSL_SUPPORT
)

target_link_libraries(loong_system
  PUBLIC
    loong_core
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
    OpenSSL::Crypto
    OpenSSL::SSL
)
