#include "dfh_node/logging.hpp"

#include <cctype>
#include <iostream>

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
        LOG_WARN("Failed to init file logger: %s", ex.what());
      } else {
        std::cerr << "Failed to init file logger: " << ex.what() << "\n";
      }
    }
  }

  LOGIT_SET_LOG_LEVEL(parse_level(log_cfg.level));
}

}  // namespace dfh_node::logging
