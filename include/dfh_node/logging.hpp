#pragma once

#include <string>

#include "dfh_node/config.hpp"

namespace dfh_node::logging {

void init_logging(const config::LoggingConfig& log_cfg);
void log_trace(const char* file, int line, const char* function, const char* fmt, ...);
void log_debug(const char* file, int line, const char* function, const char* fmt, ...);
void log_info(const char* file, int line, const char* function, const char* fmt, ...);
void log_warn(const char* file, int line, const char* function, const char* fmt, ...);
void log_error(const char* file, int line, const char* function, const char* fmt, ...);

}  // namespace dfh_node::logging

#if defined(__GNUC__)
#define DFH_LOG_FUNCTION __PRETTY_FUNCTION__
#elif defined(_MSC_VER)
#define DFH_LOG_FUNCTION __FUNCSIG__
#else
#define DFH_LOG_FUNCTION __func__
#endif

// Thin wrappers over log-it-cpp with printf-style formatting.
#define LOG_TRACE(...) \
  ::dfh_node::logging::log_trace(__FILE__, __LINE__, DFH_LOG_FUNCTION, __VA_ARGS__)
#define LOG_DEBUG(...) \
  ::dfh_node::logging::log_debug(__FILE__, __LINE__, DFH_LOG_FUNCTION, __VA_ARGS__)
#define LOG_INFO(...) \
  ::dfh_node::logging::log_info(__FILE__, __LINE__, DFH_LOG_FUNCTION, __VA_ARGS__)
#define LOG_WARN(...) \
  ::dfh_node::logging::log_warn(__FILE__, __LINE__, DFH_LOG_FUNCTION, __VA_ARGS__)
#define LOG_ERROR(...) \
  ::dfh_node::logging::log_error(__FILE__, __LINE__, DFH_LOG_FUNCTION, __VA_ARGS__)
