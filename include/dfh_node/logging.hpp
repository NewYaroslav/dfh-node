#pragma once

#include <string>

#include "dfh_node/config.hpp"
#include <LogIt.hpp>

namespace dfh_node::logging {

void init_logging(const config::LoggingConfig& log_cfg);

}  // namespace dfh_node::logging

// Thin wrappers over log-it-cpp with printf-style formatting.
#define LOG_TRACE(...) LOGIT_PRINTF_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) LOGIT_PRINTF_DEBUG(__VA_ARGS__)
#define LOG_INFO(...) LOGIT_PRINTF_INFO(__VA_ARGS__)
#define LOG_WARN(...) LOGIT_PRINTF_WARN(__VA_ARGS__)
#define LOG_ERROR(...) LOGIT_PRINTF_ERROR(__VA_ARGS__)
