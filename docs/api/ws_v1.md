# WebSocket API v1

## Endpoints
- `/ws/json` — control-message и ответы в JSON.
- `/ws/msgpack` — control-message и ответы в MessagePack.

## Handshake
- Клиент должен передать заголовок `Authorization: Bearer <token>` в Upgrade-запросе.
- При ошибке авторизации/лимитов сервер закрывает соединение с кодом `1008`.
- Anti-replay HTTP headers на этапе upgrade не используются; anti-replay начинается с первого control-message.

### WS close codes
- `1008` — policy violation на этапе upgrade. Используется при ошибках gate-проверки:
  отсутствует токен, токен невалиден, не хватает scope, превышен `rate limit` или лимит соединений.
- После успешного upgrade прикладные ошибки возвращаются не через close frame, а через обычный WS response
  с `ok=false` и полями `error_code`/`detail`.

## Схема control-message

| Поле | Тип | Обязательность | Описание |
| --- | --- | --- | --- |
| `op` | string | required | `ingest` \| `history` \| `subscribe` |
| `msg_id` | string | required | Идентификатор корреляции request/response |
| `payload` | object | required | Параметры операции |
| `payload_hash` | string | если anti-replay включён | SHA-256 payload в hex lowercase (64 символа) |
| `payload_sha256` | string | для `dfhbin` | SHA-256 binary frame в hex lowercase (64 символа) |
| `timestamp` | string | если anti-replay включён | Unix epoch ms (13 цифр) |
| `nonce` | string | если anti-replay включён | hex lowercase, 16 символов |
| `signature` | string | если anti-replay включён | HMAC-SHA256 в hex lowercase (64 символа) |

## `op=ingest` payload (structured)
- `provider` (string, non-empty)
- `symbol` (string, non-empty)
- `source` (string, non-empty)
- `tf` (string: `ticks` или `m1`)
- `block_ts` (int64)
- `payload_base64` (string, Base64)

## `op=history` payload
- `provider` (string, non-empty)
- `symbol` (string, non-empty)
- `source` (string, non-empty)
- `tf` (string: `ticks` или `m1`)
- `from_ms` (int64)
- `to_ms` (int64, строго больше `from_ms`)
- `provider_id` (optional uint32)
- `symbol_id` (optional uint32)
- Ограничение диапазона: `(to_ms - from_ms) <= ws.history_max_range_ms`

## `op=subscribe`
- Зарезервирован для будущего стриминга.
- В текущей реализации не поддержан и возвращает `error_code: "unsupported_operation"`.

## `dfhbin` flow (2 шага)
1. Клиент отправляет control-message `op=ingest` без `payload_base64`, но с `payload_sha256`.
2. Следующим сообщением клиент отправляет binary frame с raw bytes блока.
3. Сервер сверяет `sha256(binary_frame)` с `payload_sha256`.
4. При совпадении блок отправляется в ingest (high-priority очередь).

## `disk_low` в WS
- `disk_low` не приводит к HTTP `507`, потому что после upgrade обмен уже идёт внутри WS-сессии.
- Для write-операций (`op=ingest`, включая `dfhbin`) сервер возвращает обычный WS response:
  `{"msg_id":"...","ok":false,"error_code":"disk_low","detail":"Insufficient disk space"}`.
- `op=history` при `disk_low` продолжает обслуживаться.

## Canonical serialization `BlockKey`
- Для `dfhbin` payload используется объект с полями: `provider`, `symbol`, `source`, `tf`, `block_ts`.
- Канонический порядок полей фиксирован: `provider` -> `symbol` -> `source` -> `tf` -> `block_ts`.
- Типы фиксированы:
  - `provider`, `symbol`, `source`, `tf` — UTF-8 строки.
  - `block_ts` — int64.
- Для вычисления `payload_hash` рекомендуется использовать минимизированный JSON без пробелов в указанном порядке полей.

## Response message
- Успех:
  - `{"msg_id":"...","ok":true,"data":{...}}`
- Ошибка:
  - `{"msg_id":"...","ok":false,"error_code":"...","detail":"..."}`

## Коды ошибок
- `unauthorized`
- `forbidden`
- `rate_limited`
- `connection_limited`
- `anti_replay_failed`
- `anti_replay_required`
- `missing_anti_replay_fields`
- `invalid_control_message`
- `unknown_op`
- `invalid_argument`
- `not_found`
- `payload_too_large`
- `range_too_large`
- `response_too_large`
- `sha256_mismatch`
- `unexpected_binary_frame`
- `disk_low`
- `overload.high_priority_queue_full`
- `overload.low_priority_queue_full`
- `unsupported_operation`
- `timeout`
- `internal_error`

## Примеры JSON

### Control `ingest` (structured)
```json
{
  "op": "ingest",
  "msg_id": "msg-1",
  "payload_hash": "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
  "timestamp": "1760000000000",
  "nonce": "0011223344556677",
  "signature": "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
  "payload": {
    "provider": "binance",
    "symbol": "BTCUSDT",
    "source": "spot",
    "tf": "ticks",
    "block_ts": 1704067200000,
    "payload_base64": "AQID"
  }
}
```

### Control `history`
```json
{
  "op": "history",
  "msg_id": "msg-2",
  "payload_hash": "cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
  "timestamp": "1760000001000",
  "nonce": "8899aabbccddeeff",
  "signature": "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
  "payload": {
    "provider": "binance",
    "symbol": "BTCUSDT",
    "source": "spot",
    "tf": "ticks",
    "from_ms": 1704067200000,
    "to_ms": 1704070800000
  }
}
```

### `dfhbin` sequence
```json
{
  "op": "ingest",
  "msg_id": "msg-3",
  "payload_hash": "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",
  "payload_sha256": "f8ef5e3e7a3d3f0f7f1bbf5f778c0f0d00ed9f0a3a57c9c9b86d5142dd2f90c5",
  "timestamp": "1760000002000",
  "nonce": "0102030405060708",
  "signature": "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
  "payload": {
    "provider": "binance",
    "symbol": "BTCUSDT",
    "source": "spot",
    "tf": "ticks",
    "block_ts": 1704067200000
  }
}
```

### Error response
```json
{
  "msg_id": "msg-3",
  "ok": false,
  "error_code": "sha256_mismatch",
  "detail": "binary frame hash does not match"
}
```

## Лимиты
- `ws.max_payload_bytes` (по умолчанию `10000000`, 10 MB).
- `auth.ws_max_connections` (лимит одновременных WS-соединений на fingerprint).
- `ws.max_ws_connections_total` (глобальный лимит активных WS-соединений по всем fingerprint).
- `ws.history_max_range_ms` ограничивает диапазон `op=history`.
- `ws.history_max_bytes` ограничивает суммарный raw размер `payload` в ответе `history` до сериализации.
- `ws.request_timeout_ms` (по умолчанию `30000`, `0` отключает таймаут).
