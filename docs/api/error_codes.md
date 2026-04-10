# Error Codes Reference

Документ фиксирует стабильные `error_code`, которые уже используются в коде transport/gate-слоя.
Источник истины: `http_error_map.cpp`, `auth_service.hpp`, `ws_message_handler.cpp`, `ws_protocol.cpp`.

## HTTP error codes

| HTTP status | error_code | Описание | Retry? |
| --- | --- | --- | --- |
| 400 | `invalid_query_param` | Ошибка query/body DTO или обязательных полей | нет |
| 400 | `invalid_json` | Тело запроса не является валидным JSON | нет |
| 400 | `invalid_argument` | Адаптер отклонил аргументы запроса | нет |
| 400 | `unsupported_operation` | Неподдерживаемая операция/`TaskKind` | нет |
| 400 | `missing_anti_replay_headers` | Anti-replay headers отсутствуют или неполные | нет |
| 400 | `missing_anti_replay_fields` | Anti-replay поля отсутствуют или неполные | нет |
| 400 | `range_too_large` | Диапазон history превышает `history_max_range_ms` | нет |
| 401 | `unauthorized` | Нет токена или токен невалиден | нет |
| 401 | `anti_replay_failed` | Подпись, формат полей, окно времени или nonce не прошли проверку | нет |
| 403 | `forbidden` | Недостаточно scope | нет |
| 403 | `anti_replay_required` | Для этого scope anti-replay обязателен, но отключён политикой | нет |
| 404 | `not_found` | Ресурс или блок не найден | нет |
| 409 | `duplicate_name` | Имя динамического ключа уже занято | нет |
| 413 | `payload_too_large` | Тело запроса превышает `max_payload_bytes` | нет |
| 413 | `response_too_large` | Ответ history превышает `history_max_bytes` | нет |
| 429 | `rate_limited` | Превышен RPS-лимит | да, exponential backoff |
| 429 | `connection_limited` | Превышен лимит WS-соединений | да, exponential backoff |
| 503 | `queue_full` | Очередь scheduler заполнена | да, exponential backoff |
| 504 | `timeout` | Истёк `request_timeout_ms` | да, exponential backoff |
| 507 | `disk_low` | Запись заблокирована из-за нехватки свободного места | да, после проверки `/v1/status` |
| 500 | `internal_error` | Внутренняя ошибка transport/adapter слоя | нет |

## WS error codes

| error_code | Описание |
| --- | --- |
| `unauthorized` | Upgrade или обработка сообщения отклонены из-за отсутствующего/невалидного токена |
| `forbidden` | Недостаточно scope для операции |
| `rate_limited` | Превышен RPS-лимит |
| `connection_limited` | Превышен лимит WS-соединений |
| `unsupported_operation` | Операция не поддержана; в частности, `op=subscribe` пока не реализован |
| `anti_replay_failed` | Формат anti-replay полей, подпись, окно времени или replay nonce не прошли проверку |
| `anti_replay_required` | Для этого scope anti-replay обязателен, но отключён политикой |
| `missing_anti_replay_fields` | В control-message не хватает anti-replay полей |
| `invalid_control_message` | Control-message невалиден как JSON/MessagePack object |
| `unknown_op` | Поле `op` не распознано |
| `invalid_argument` | DTO или adapter-аргументы не прошли проверку |
| `not_found` | Запрошенный блок/ресурс не найден |
| `payload_too_large` | Binary frame превышает `ws.max_payload_bytes` |
| `range_too_large` | Диапазон `op=history` превышает `ws.history_max_range_ms` |
| `response_too_large` | Ответ history превышает `ws.history_max_bytes` |
| `sha256_mismatch` | Хеш binary frame не совпадает с `payload_sha256` |
| `unexpected_binary_frame` | Получен binary frame без pending `dfhbin` control-message |
| `disk_low` | Write-операция отклонена из-за нехватки свободного места |
| `overload.high_priority_queue_full` | Переполнена high-priority очередь |
| `overload.low_priority_queue_full` | Переполнена low-priority очередь |
| `timeout` | Истёк `ws.request_timeout_ms` |
| `internal_error` | Внутренняя ошибка transport/adapter слоя |

## GateErrorCode -> HTTP / WS mapping

| GateErrorCode | HTTP mapping | WS mapping |
| --- | --- | --- |
| `Unauthorized` | `401 unauthorized` | `unauthorized` |
| `Forbidden` | `403 forbidden` | `forbidden` |
| `RateLimited` | `429 rate_limited` | `rate_limited` |
| `ConnectionLimited` | `429 connection_limited` | `connection_limited` |
| `UnsupportedOperation` | `400 unsupported_operation` | `unsupported_operation` |
| `AntiReplayFailed` | `401 anti_replay_failed` | `anti_replay_failed` |
| `AntiReplayRequired` | `403 anti_replay_required` | `anti_replay_required` |
| `MissingAntiReplayHeaders` | `400 missing_anti_replay_headers` | не используется в текущем WS runtime |
| `MissingAntiReplayFields` | `400 missing_anti_replay_fields` | `missing_anti_replay_fields` |

## Примечания по ретраям

- Ретраить стоит только `429`, `503`, `504` и `507`.
- Для `429`, `503`, `504` рекомендуем `exponential backoff` с `jitter`.
- Для `507` сначала проверьте `/v1/status.disk_low`; retry имеет смысл только после восстановления места на диске.
