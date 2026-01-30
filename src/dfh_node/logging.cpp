#include "dfh_node/logging.hpp"

#include <cctype>
#include <cstdarg>
#include <iostream>
#include <vector>

#include <LogIt.hpp>

namespace dfh_node::logging {

namespace {

logit::LogLevel parse_level(const std::string& level) {
  std::string lower;
  lower.reserve(level.size());
  for (char ch : level) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  if (lower == "trace") {
    return logit::LogLevel::LOG_LVL_TRACE;
  }
  if (lower == "debug") {
    return logit::LogLevel::LOG_LVL_DEBUG;
  }
  if (lower == "info") {
    return logit::LogLevel::LOG_LVL_INFO;
  }
  if (lower == "warn") {
    return logit::LogLevel::LOG_LVL_WARN;
  }
  if (lower == "error") {
    return logit::LogLevel::LOG_LVL_ERROR;
  }
  return logit::LogLevel::LOG_LVL_INFO;
}

std::string vformat(const char* fmt, va_list args) {
  std::vector<char> buffer(1024);
  for (;;) {
    va_list args_copy;
    va_copy(args_copy, args);
    const int res = vsnprintf(buffer.data(), buffer.size(), fmt, args_copy);
    va_end(args_copy);

    if ((res >= 0) && (res < static_cast<int>(buffer.size()))) {
      return std::string(buffer.data());
    }

    const std::size_t next_size =
        res < 0 ? buffer.size() * 2 : static_cast<std::size_t>(res) + 1;
    buffer.clear();
    buffer.resize(next_size);
  }
}

void log_message(logit::LogLevel level,
                 const char* file,
                 int line,
                 const char* function,
                 const char* fmt,
                 va_list args) {
  const std::string message = vformat(fmt, args);
  logit::Logger::get_instance().log_and_return(
      logit::LogRecord{level,
                       LOGIT_CURRENT_TIMESTAMP_MS(),
                       logit::make_relative(file, LOGIT_BASE_PATH),
                       line,
                       function,
                       message,
                       {},
                       -1,
                       false});
}

}  // namespace

void init_logging(const config::LoggingConfig& log_cfg) {
  static bool initialized = false;
  if (initialized) {
    return;
  }
  initialized = true;

  if (log_cfg.console) {
    LOGIT_ADD_CONSOLE_DEFAULT();
  }

  if (!log_cfg.file_path.empty()) {
    try {
      LOGIT_ADD_FILE_LOGGER(log_cfg.file_path, true, LOGIT_FILE_LOGGER_AUTO_DELETE_DAYS,
                            LOGIT_FILE_LOGGER_PATTERN);
    } catch (const std::exception& ex) {
      if (log_cfg.console) {
        log_warn(__FILE__, __LINE__, DFH_LOG_FUNCTION, "Failed to init file logger: %s",
                 ex.what());
      } else {
        std::cerr << "Failed to init file logger: " << ex.what() << "\n";
      }
    }
  }

  LOGIT_SET_LOG_LEVEL(parse_level(log_cfg.level));
}

void log_trace(const char* file, int line, const char* function, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_message(logit::LogLevel::LOG_LVL_TRACE, file, line, function, fmt, args);
  va_end(args);
}

void log_debug(const char* file, int line, const char* function, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_message(logit::LogLevel::LOG_LVL_DEBUG, file, line, function, fmt, args);
  va_end(args);
}

void log_info(const char* file, int line, const char* function, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_message(logit::LogLevel::LOG_LVL_INFO, file, line, function, fmt, args);
  va_end(args);
}

void log_warn(const char* file, int line, const char* function, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_message(logit::LogLevel::LOG_LVL_WARN, file, line, function, fmt, args);
  va_end(args);
}

void log_error(const char* file, int line, const char* function, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  log_message(logit::LogLevel::LOG_LVL_ERROR, file, line, function, fmt, args);
  va_end(args);
}

}  // namespace dfh_node::logging
