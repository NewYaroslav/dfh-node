# HTTP API v1

Документ фиксирует текущий контракт HTTP transport для `dfh-node`.

## Общие правила
- Базовый префикс: `/v1`.
- Формат ошибок: JSON c полями:
  - `error` — стабильный код для клиентской логики.
  - `detail` — нестабильная диагностическая строка (только для логов/диагностики).
- Авторизация: `Authorization: Bearer <token>`.
- Anti-replay (когда требуется): `X-DFH-Timestamp`, `X-DFH-Nonce`, `X-DFH-Signature`.
- Admin CRUD вынесен в отдельный документ: `docs/api/admin_v1.md`.
- Ops endpoints `/health`, `/ready`, `/metrics` доступны без префикса `/v1`.

## POST /v1/ingest
- Назначение: записать пачку блоков истории.
- Требуемый scope: `write`.
- Anti-replay: обязателен по политике `security.anti_replay.require_for_scopes`.
- `Content-Type`: `application/json`.

### Тело запроса
```json
[
  {
    "key": {
      "provider": "binance",
      "symbol": "BTCUSDT",
      "source": "spot",
      "tf": "ticks",
      "block_ts": 1704067200000
    },
    "payload_base64": "AQIDBA=="
  }
]
```

### Успешный ответ (200)
```json
{
  "results": [
    { "status": "ok", "error_code": "" }
  ]
}
```

### Пример
```bash
curl -X POST "http://127.0.0.1:8080/v1/ingest" \
  -H "Authorization: Bearer <token>" \
  -H "X-DFH-Timestamp: 1700000000000" \
  -H "X-DFH-Nonce: a1b2c3d4e5f67890" \
  -H "X-DFH-Signature: <hmac_sha256_hex>" \
  -H "Content-Type: application/json" \
  --data-binary '[{"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1704067200000},"payload_base64":"AQIDBA=="}]'
```

## GET /v1/history
- Назначение: выгрузка истории блоков.
- Требуемый scope: `read`.
- Anti-replay: опционален, зависит от `security.anti_replay.require_for_scopes`.

### Query-параметры
- `provider` (string, обязательно)
- `symbol` (string, обязательно)
- `source` (string, обязательно)
- `tf` (string, обязательно): `ticks` | `m1`
- `from_ms` (int64, обязательно, включительно)
- `to_ms` (int64, обязательно, исключительно)
- `format` (string, опционально): `csv` (по умолчанию) | `dfhbin`
- `provider_id` (uint32, опционально)
- `symbol_id` (uint32, опционально)

Ограничения:
- `from_ms < to_ms`
- `(to_ms - from_ms) <= http.history_max_range_ms`
- итоговый размер ответа `<= http.history_max_bytes`

### Успешный ответ (200)
- `format=csv`:
  - `Content-Type: text/csv`
  - `Content-Disposition: attachment; filename="history.csv"`
- `format=dfhbin`:
  - `Content-Type: application/octet-stream`
  - `Content-Disposition: attachment; filename="history.dfhbin"`

### Примеры
```bash
curl "http://127.0.0.1:8080/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000&format=csv" \
  -H "Authorization: Bearer <token>"
```

```bash
curl "http://127.0.0.1:8080/v1/history?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000&format=dfhbin" \
  -H "Authorization: Bearer <token>" \
  -o history.dfhbin
```

## GET /v1/status
- Назначение: получить runtime-статус ноды.
- Требуемый scope: `read`.
- Anti-replay: не используется (`nullptr` в `authorize_http`).

### Успешный ответ (200)
```json
{
  "node_id": "node-01",
  "version": "0.1.0",
  "build_info": "...",
  "uptime_ms": 12345,
  "peers_count": 0,
  "env": "dev",
  "workers_count": 4,
  "queues": {
    "high_priority_queue": {
      "current_size": 0,
      "capacity": 10000,
      "rejected_count": 0,
      "dropped_count": 0,
      "total_enqueued": 0,
      "total_processed": 0,
      "avg_wait_ms": 0.0
    },
    "low_priority_queue": {
      "current_size": 0,
      "capacity": 5000,
      "rejected_count": 0,
      "dropped_count": 0,
      "total_enqueued": 0,
      "total_processed": 0,
      "avg_wait_ms": 0.0
    }
  },
  "disk_free_bytes": 123456789,
  "disk_low": false,
  "mdbx_keys_active": 3
}
```

### Пример
```bash
curl "http://127.0.0.1:8080/v1/status" \
  -H "Authorization: Bearer <token>"
```

## Таблица error-кодов
| HTTP | error | Где возникает |
| --- | --- | --- |
| 400 | `invalid_query_param` | Ошибка query/body DTO |
| 400 | `invalid_json` | Ошибка JSON в ingest |
| 400 | `invalid_argument` | Ошибка аргументов адаптера |
| 400 | `unsupported_operation` | Неподдерживаемый `TaskKind` |
| 400 | `missing_anti_replay_headers` | Неполные anti-replay HTTP headers |
| 400 | `missing_anti_replay_fields` | Неполные anti-replay WS fields |
| 400 | `range_too_large` | Диапазон history превышает `history_max_range_ms` |
| 401 | `unauthorized` | Нет токена/невалидный токен |
| 401 | `anti_replay_failed` | Проверка anti-replay не пройдена |
| 403 | `forbidden` | Недостаточно scope |
| 403 | `anti_replay_required` | Anti-replay выключен, но обязателен для scope |
| 404 | `not_found` | Ресурс/блок не найден |
| 413 | `payload_too_large` | Тело запроса превышает `max_payload_bytes` |
| 413 | `response_too_large` | Размер ответа history превышает `history_max_bytes` |
| 429 | `rate_limited` | Превышен RPS лимит |
| 429 | `connection_limited` | Превышен лимит WS соединений |
| 507 | `disk_low` | Запись запрещена из-за нехватки диска |
| 503 | `queue_full` | Очередь scheduler заполнена |
| 504 | `timeout` | Сработал timeout deferred reply |
| 500 | `internal_error` | Внутренняя ошибка |

