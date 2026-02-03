/**
 * @file config.hpp
 * @brief Структуры конфигурации ноды и фабрика дефолтных значений.
 * @details Конфигурация не валидируется сама по себе; см. config_validator.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace dfh_node::config {

/// @brief Параметры HTTP-интерфейса.
/// @details Ожидается проверка диапазонов валидатором.
struct HttpConfig {
    std::string bind_host = "0.0.0.0"; ///< Хост/интерфейс для bind.
    int port = 8080; ///< TCP-порт (ожидается 1..65535).
    std::int64_t max_payload_bytes = 10000000; ///< Лимит тела запроса, байты.
};

/// @brief Параметры WebSocket-интерфейса.
/// @details По умолчанию разделяет порт с HTTP.
struct WsConfig {
    std::string bind_host = "0.0.0.0"; ///< Хост/интерфейс для bind.
    int port = 8081; ///< TCP-порт (ожидается 1..65535).
    std::int64_t max_payload_bytes = 10000000; ///< Лимит размера фрейма, байты.
};

/// @brief Настройки внутренних очередей и воркеров.
/// @details Поля используются планировщиком ingest/history.
struct QueuesConfig {
    std::int64_t ingest_capacity = 10000; ///< Ёмкость ingest-очереди, элементов.
    std::int64_t history_capacity = 5000; ///< Ёмкость history-очереди, элементов.
    int workers = 4; ///< Количество рабочих потоков.
};

/// @brief Параметры anti-replay проверки.
/// @details Все значения интерпретируются в миллисекундах.
struct AntiReplayConfig {
    bool enabled = true; ///< Включить проверку anti-replay.
    std::int64_t max_skew_ms = 5000; ///< Допустимое расхождение времени, мс.
    std::int64_t nonce_ttl_ms = 60000; ///< Время жизни nonce, мс.
    std::int64_t nonce_capacity = 10000; ///< Максимум хранимых nonce.
};

/// @brief Безопасность и секреты ноды.
/// @details server_secret обязателен и валидируется отдельно.
struct SecurityConfig {
    std::string server_secret; ///< Секрет сервера (HMAC).
    AntiReplayConfig anti_replay{}; ///< Параметры anti-replay.
};

/// @brief Параметры хранения на диске.
/// @details min_free_bytes используется для защиты от переполнения диска.
struct StorageConfig {
    std::string path = "./data"; ///< Путь к каталогу данных.
    std::int64_t min_free_bytes = 2000000000; ///< Минимум свободного места, байты.
};

/// @brief Конфигурация логирования.
/// @details Уровень валидируется на допустимые значения.
struct LoggingConfig {
    std::string level = "info"; ///< Уровень логирования (trace|debug|info|warn|error).
    bool console = true; ///< Включить вывод в консоль.
    std::string file_path; ///< Путь к файлу лога (пусто = не писать в файл).
};

/// @brief Описание peer-ноды для синхронизации.
/// @details url ожидается с префиксом http/https.
struct PeerConfig {
    std::string id; ///< Уникальный идентификатор пира.
    std::string url; ///< Базовый URL пира.
};

/// @brief Полная конфигурация ноды.
/// @details Валидируется через validate; значения по умолчанию задаются в default_config.
struct Config {
    int schema_version = 1; ///< Версия схемы (ожидается 1).
    std::string node_id; ///< Идентификатор ноды (не пустой).
    std::string env; ///< Окружение (dev|staging|prod).
    HttpConfig http{}; ///< HTTP-настройки.
    WsConfig ws{}; ///< WS-настройки.
    QueuesConfig queues{}; ///< Очереди и воркеры.
    SecurityConfig security{}; ///< Безопасность и секреты.
    std::vector<PeerConfig> peers{}; ///< Список peer-нод.
    StorageConfig storage{}; ///< Настройки хранения.
    LoggingConfig logging{}; ///< Настройки логирования.
};

/// @brief Формирует набор дефолтных настроек.
/// @return Конфигурация с безопасными значениями по умолчанию.
/// @throws Не бросает.
/// @note Потокобезопасно, возвращает новую структуру.
Config default_config();

} // namespace dfh_node::config
