/// \file config.hpp
/// \brief Структуры конфигурации ноды и фабрика дефолтных значений.
/// \details Конфигурация не валидируется сама по себе; см. config_validator.

#pragma once

#include "scope.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dfh_node::config {

/// \brief Параметры HTTP-интерфейса.
/// \details Ожидается проверка диапазонов валидатором.
struct HttpConfig {
    std::string bind_host = "0.0.0.0"; ///< Хост/интерфейс для bind.
    int port = 8080;                   ///< TCP-порт (ожидается 1..65535).
    std::int64_t max_payload_bytes = 10000000; ///< Лимит тела запроса, байты.
};

/// \brief Параметры WebSocket-интерфейса.
/// \details По умолчанию разделяет порт с HTTP.
struct WsConfig {
    std::string bind_host = "0.0.0.0"; ///< Хост/интерфейс для bind.
    int port = 8081;                   ///< TCP-порт (ожидается 1..65535).
    std::int64_t max_payload_bytes = 10000000; ///< Лимит размера фрейма, байты.
};

/// \brief Настройки внутренних очередей и воркеров.
/// \details Поля используются планировщиком high/low приоритета.
struct QueuesConfig {
    std::int64_t high_capacity = 10000; ///< Ёмкость high-priority очереди, элементов.
    std::int64_t low_capacity  = 5000;  ///< Ёмкость low-priority очереди, элементов.
    int workers = 4; ///< Количество рабочих потоков.
};

/// \brief Параметры anti-replay проверки.
/// \details Все значения интерпретируются в миллисекундах.
struct AntiReplayConfig {
    bool enabled = true;                 ///< Включить проверку anti-replay.
    std::int64_t max_skew_ms  = 5000;    ///< Допустимое расхождение времени, мс.
    std::int64_t nonce_ttl_ms = 60000;   ///< Время жизни nonce, мс.
    std::int64_t nonce_capacity = 10000; ///< Максимум хранимых nonce.
    ScopeMask require_for_scopes = 0;    ///< Битмаска scope, где anti-replay обязателен.
};

/// \brief Безопасность и секреты ноды.
/// \details server_secret обязателен и валидируется отдельно.
struct SecurityConfig {
    std::string server_secret;      ///< Секрет сервера (HMAC).
    AntiReplayConfig anti_replay{}; ///< Параметры anti-replay.
};

/// \brief API key запись в конфиге без plaintext-токена.
/// \details Токен преобразуется в fingerprint на этапе загрузки конфига.
struct ApiKeyEntry {
    std::string fingerprint;  ///< HMAC-SHA256(server_secret, token) в hex.
    ScopeMask scope_mask = 0; ///< Битовая маска разрешённых scope.
    std::optional<std::int64_t> expires_at_ms; ///< Время истечения Unix epoch ms.
    std::int64_t rps_limit = 100;         ///< Индивидуальный лимит запросов в секунду.
    std::int64_t ws_max_connections = 10; ///< Лимит одновременных WS-соединений.
};

/// \brief Конфигурация авторизации и rate-limits.
/// \details Значения по умолчанию применяются к ключам без переопределений.
struct AuthConfig {
    std::int64_t cache_ttl_ms = 60000;    ///< TTL auth-кэша в миллисекундах.
    std::int64_t rps_limit = 100;         ///< Дефолтный лимит запросов в секунду.
    std::int64_t ws_max_connections = 10; ///< Дефолтный лимит WS-соединений.
    std::int64_t rate_limit_window_ms = 1000; ///< Окно rate-limit в миллисекундах.
    std::vector<ApiKeyEntry> api_keys{};  ///< Ключи в виде fingerprint без plaintext.
};

/// \brief Параметры хранения на диске.
/// \details min_free_bytes используется для защиты от переполнения диска.
struct StorageConfig {
    std::string path = "./data"; ///< Путь к каталогу данных.
    std::int64_t min_free_bytes = 2000000000; ///< Минимум свободного места, байты.
};

/// \brief Конфигурация логирования.
/// \details Уровень валидируется на допустимые значения.
struct LoggingConfig {
    std::string level = "info"; ///< Уровень логирования (trace|debug|info|warn|error).
    bool console = true;        ///< Включить вывод в консоль.
    std::string file_path;      ///< Путь к файлу лога (пусто = не писать в файл).
};

/// \brief Описание peer-ноды для синхронизации.
/// \details url ожидается с префиксом http/https.
struct PeerConfig {
    std::string id;  ///< Уникальный идентификатор пира.
    std::string url; ///< Базовый URL пира.
};

/// \brief Полная конфигурация ноды.
/// \details Валидируется через validate; значения по умолчанию задаются в
/// default_config.
struct Config {
    int schema_version = 1;          ///< Версия схемы (ожидается 1).
    std::string node_id;             ///< Идентификатор ноды (не пустой).
    std::string env;                 ///< Окружение (dev|staging|prod).
    HttpConfig http{};               ///< HTTP-настройки.
    WsConfig ws{};                   ///< WS-настройки.
    QueuesConfig queues{};           ///< Очереди и воркеры.
    SecurityConfig security{};       ///< Безопасность и секреты.
    AuthConfig auth{};               ///< Авторизация и rate limiting.
    std::vector<PeerConfig> peers{}; ///< Список peer-нод.
    StorageConfig storage{};         ///< Настройки хранения.
    LoggingConfig logging{};         ///< Настройки логирования.
};

/// \brief Формирует набор дефолтных настроек.
/// \return Конфигурация с безопасными значениями по умолчанию.
/// \throws Не бросает.
/// \note Потокобезопасно, возвращает новую структуру.
Config default_config();

} // namespace dfh_node::config
