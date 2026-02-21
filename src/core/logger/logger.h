// Copyright 2026 Loong AI NVR Project

#ifndef LOONG_CORE_LOGGER_LOGGER_H_
#define LOONG_CORE_LOGGER_LOGGER_H_

#include <string>

#include "spdlog/spdlog.h"

namespace loong::core {

/// Initialize the global logger with the given name and level.
/// @param json_format If true, output logs in JSON format for structured logging.
void InitLogger(const std::string& logger_name,
                spdlog::level::level_enum level = spdlog::level::info,
                bool json_format = false);

}  // namespace loong

#endif  // LOONG_CORE_LOGGER_LOGGER_H_
