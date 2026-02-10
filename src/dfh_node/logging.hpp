/// \file logging.hpp
/// \brief Инициализация логирования и алиасы макросов log-it-cpp.
/// \details Макросы не оборачиваются функциями ради compile-time gating.
///
#pragma once

#include "config.hpp"

namespace dfh_node::logging {

/// \brief Инициализирует log-it-cpp согласно конфигурации.
/// \param log_cfg Настройки логирования.
/// \return Ничего не возвращает.
/// \throws Не бросает (ошибки файла обрабатываются внутри).
/// \note Побочные эффекты: настройка логгеров и уровня логирования.
/// \note Не потокобезопасно; вызывать в одном потоке при старте.
void init_logging(const config::LoggingConfig &log_cfg);

} // namespace dfh_node::logging

// Алиасы макросов log-it-cpp на уровне проекта (без функций-обёрток).
// ВАЖНО: любой TU, использующий DFH_* макросы, должен включать <LogIt.hpp>.
#define DFH_TRACE(...) LOGIT_TRACE(__VA_ARGS__)
#define DFH_DEBUG(...) LOGIT_DEBUG(__VA_ARGS__)
#define DFH_INFO(...) LOGIT_INFO(__VA_ARGS__)
#define DFH_WARN(...) LOGIT_WARN(__VA_ARGS__)
#define DFH_ERROR(...) LOGIT_ERROR(__VA_ARGS__)
#define DFH_FATAL(...) LOGIT_FATAL(__VA_ARGS__)

// Алиасы для printf-стиля форматирования.
#define DFH_PRINTF_TRACE(fmt, ...) LOGIT_PRINTF_TRACE(fmt, __VA_ARGS__)
#define DFH_PRINTF_DEBUG(fmt, ...) LOGIT_PRINTF_DEBUG(fmt, __VA_ARGS__)
#define DFH_PRINTF_INFO(fmt, ...) LOGIT_PRINTF_INFO(fmt, __VA_ARGS__)
#define DFH_PRINTF_WARN(fmt, ...) LOGIT_PRINTF_WARN(fmt, __VA_ARGS__)
#define DFH_PRINTF_ERROR(fmt, ...) LOGIT_PRINTF_ERROR(fmt, __VA_ARGS__)
#define DFH_PRINTF_FATAL(fmt, ...) LOGIT_PRINTF_FATAL(fmt, __VA_ARGS__)

// Алиасы для fmt-стиля форматирования (LOGIT_USE_FMT_LIB).
#define DFH_FORMAT_TRACE(fmt, ...) LOGIT_FORMAT_TRACE(fmt, __VA_ARGS__)
#define DFH_FORMAT_DEBUG(fmt, ...) LOGIT_FORMAT_DEBUG(fmt, __VA_ARGS__)
#define DFH_FORMAT_INFO(fmt, ...) LOGIT_FORMAT_INFO(fmt, __VA_ARGS__)
#define DFH_FORMAT_WARN(fmt, ...) LOGIT_FORMAT_WARN(fmt, __VA_ARGS__)
#define DFH_FORMAT_ERROR(fmt, ...) LOGIT_FORMAT_ERROR(fmt, __VA_ARGS__)
#define DFH_FORMAT_FATAL(fmt, ...) LOGIT_FORMAT_FATAL(fmt, __VA_ARGS__)

// Алиасы для конкатенации через print-стиль.
#define DFH_PRINT_TRACE(...) LOGIT_PRINT_TRACE(__VA_ARGS__)
#define DFH_PRINT_DEBUG(...) LOGIT_PRINT_DEBUG(__VA_ARGS__)
#define DFH_PRINT_INFO(...) LOGIT_PRINT_INFO(__VA_ARGS__)
#define DFH_PRINT_WARN(...) LOGIT_PRINT_WARN(__VA_ARGS__)
#define DFH_PRINT_ERROR(...) LOGIT_PRINT_ERROR(__VA_ARGS__)
#define DFH_PRINT_FATAL(...) LOGIT_PRINT_FATAL(__VA_ARGS__)
