#pragma once

#include "dfh_node/config.hpp"

namespace dfh_node::logging {

void init_logging(const config::LoggingConfig &log_cfg);

} // namespace dfh_node::logging

// Project-level aliases over log-it-cpp macros (no wrapper functions).
// NOTE: Any translation unit that uses DFH_* macros must include <LogIt.hpp>.
#define DFH_TRACE(...) LOGIT_TRACE(__VA_ARGS__)
#define DFH_DEBUG(...) LOGIT_DEBUG(__VA_ARGS__)
#define DFH_INFO(...) LOGIT_INFO(__VA_ARGS__)
#define DFH_WARN(...) LOGIT_WARN(__VA_ARGS__)
#define DFH_ERROR(...) LOGIT_ERROR(__VA_ARGS__)
#define DFH_FATAL(...) LOGIT_FATAL(__VA_ARGS__)

// printf-style formatting aliases.
#define DFH_PRINTF_TRACE(fmt, ...) LOGIT_PRINTF_TRACE(fmt, __VA_ARGS__)
#define DFH_PRINTF_DEBUG(fmt, ...) LOGIT_PRINTF_DEBUG(fmt, __VA_ARGS__)
#define DFH_PRINTF_INFO(fmt, ...) LOGIT_PRINTF_INFO(fmt, __VA_ARGS__)
#define DFH_PRINTF_WARN(fmt, ...) LOGIT_PRINTF_WARN(fmt, __VA_ARGS__)
#define DFH_PRINTF_ERROR(fmt, ...) LOGIT_PRINTF_ERROR(fmt, __VA_ARGS__)
#define DFH_PRINTF_FATAL(fmt, ...) LOGIT_PRINTF_FATAL(fmt, __VA_ARGS__)

// fmt-style formatting aliases (LOGIT_USE_FMT_LIB).
#define DFH_FORMAT_TRACE(fmt, ...) LOGIT_FORMAT_TRACE(fmt, __VA_ARGS__)
#define DFH_FORMAT_DEBUG(fmt, ...) LOGIT_FORMAT_DEBUG(fmt, __VA_ARGS__)
#define DFH_FORMAT_INFO(fmt, ...) LOGIT_FORMAT_INFO(fmt, __VA_ARGS__)
#define DFH_FORMAT_WARN(fmt, ...) LOGIT_FORMAT_WARN(fmt, __VA_ARGS__)
#define DFH_FORMAT_ERROR(fmt, ...) LOGIT_FORMAT_ERROR(fmt, __VA_ARGS__)
#define DFH_FORMAT_FATAL(fmt, ...) LOGIT_FORMAT_FATAL(fmt, __VA_ARGS__)

// Print-style concatenation aliases.
#define DFH_PRINT_TRACE(...) LOGIT_PRINT_TRACE(__VA_ARGS__)
#define DFH_PRINT_DEBUG(...) LOGIT_PRINT_DEBUG(__VA_ARGS__)
#define DFH_PRINT_INFO(...) LOGIT_PRINT_INFO(__VA_ARGS__)
#define DFH_PRINT_WARN(...) LOGIT_PRINT_WARN(__VA_ARGS__)
#define DFH_PRINT_ERROR(...) LOGIT_PRINT_ERROR(__VA_ARGS__)
#define DFH_PRINT_FATAL(...) LOGIT_PRINT_FATAL(__VA_ARGS__)
