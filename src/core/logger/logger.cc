// Copyright 2026 Loong AI NVR Project

#include "core/logger/logger.h"

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace loong::core {

void InitLogger(const std::string& logger_name,
                spdlog::level::level_enum level,
                bool json_format) {
  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(level);

  auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
      logger_name + ".log", 1024 * 1024 * 10, 3);
  file_sink->set_level(spdlog::level::debug);

  auto logger = std::make_shared<spdlog::logger>(
      logger_name,
      spdlog::sinks_init_list{console_sink, file_sink});
  logger->set_level(level);

  if (json_format) {
    // JSON structured log format for production/log aggregators
    logger->set_pattern(
        R"({"ts":"%Y-%m-%dT%H:%M:%S.%eZ","level":"%l","tid":%t,"msg":"%v"})");
    file_sink->set_pattern(
        R"({"ts":"%Y-%m-%dT%H:%M:%S.%eZ","level":"%l","tid":%t,"msg":"%v"})");
  } else {
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
  }

  spdlog::set_default_logger(logger);
  spdlog::info("Logger '{}' initialized (json={})", logger_name, json_format);
}

}  // namespace loong::core
