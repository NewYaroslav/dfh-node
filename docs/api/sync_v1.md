# Sync API v1

Документ фиксирует контракт межнодовой pull-синхронизации `dfh-node`.

## Обзор
- Sync v1 использует pull-модель: нода периодически опрашивает статически заданных peers из `config.peers`.
- Модель согласованности: eventual consistency.
- Входящие sync-endpoint'ы активны всегда, даже если `sync.enabled = false`.
- Pull loop запускается только при `sync.enabled = true` и непустом `peers`.

## Авторизация
- Требуемый scope: `sync`.
- `admin` допускается как override.
- Для всех `/sync/*` endpoint'ов anti-replay обязателен, включая `GET`.

Обязательные HTTP-заголовки:
- `Authorization: Bearer <token>`
- `X-DFH-Timestamp: <unix_epoch_ms>`
- `X-DFH-Nonce: <16 hex>`
- `X-DFH-Signature: <64 hex>`

Подпись рассчитывается по обычной HTTP canonical-строке:

```text
METHOD\n
PATH\n
QUERY_STRING\n
TIMESTAMP\n
NONCE\n
BODY_HASH
```

Ключ подписи:
- `signing_key = SHA256(token)` в raw-виде.

## POST /sync/meta
- Назначение: вернуть список метаданных блоков peer-ноды.
- `Content-Type`: `application/json`.
- disk_low не блокирует этот endpoint.

### Тело запроса

Все поля опциональны. Пустой объект означает "все блоки".

```json
{
  "provider": "binance",
  "symbol": "BTCUSDT",
  "source": "spot",
  "tf": "ticks",
  "from_block_ts": 1700000000000,
  "to_block_ts": 1700003600000
}
```

### Успешный ответ (200)

```json
{
  "blocks": [
    {
      "provider": "binance",
      "symbol": "BTCUSDT",
      "source": "spot",
      "tf": "ticks",
      "block_ts": 1700000000000,
      "first_ts": 1700000000000,
      "last_ts": 1700003599999,
      "record_count": 15234,
      "updated_at": 1700003600100,
      "hash": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  ]
}
```

### Пример

```bash
curl -X POST "http://127.0.0.1:8080/sync/meta" \
  -H "Authorization: Bearer <sync-token>" \
  -H "X-DFH-Timestamp: 1700000000000" \
  -H "X-DFH-Nonce: a1b2c3d4e5f67890" \
  -H "X-DFH-Signature: <hmac_sha256_hex>" \
  -H "Content-Type: application/json" \
  --data-binary '{}'
```

## GET /sync/block
- Назначение: вернуть raw `dfhbin`-блок по ключу.
- Ответ `200` содержит бинарное тело.
- disk_low не блокирует этот endpoint.

### Query-параметры
- `provider` (string, обязательно)
- `symbol` (string, обязательно)
- `source` (string, обязательно)
- `tf` (string, обязательно): `ticks` | `m1`
- `block_ts` (int64, обязательно)

### Успешный ответ (200)
- `Content-Type: application/octet-stream`
- тело ответа: raw `dfhbin`

### Ошибки
- `404 not_found`, если блок отсутствует
- `400 invalid_query_param`, если не хватает query-поля или `tf` невалиден

### Пример

```bash
curl "http://127.0.0.1:8080/sync/block?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&block_ts=1700000000000" \
  -H "Authorization: Bearer <sync-token>" \
  -H "X-DFH-Timestamp: 1700000000000" \
  -H "X-DFH-Nonce: a1b2c3d4e5f67890" \
  -H "X-DFH-Signature: <hmac_sha256_hex>" \
  -o block.dfhbin
```

## GET /sync/status
- Назначение: вернуть operational-state sync runtime.
- Endpoint доступен даже если pull loop не запущен.

### Успешный ответ (200)

```json
{
  "sync_enabled": true,
  "peers_count": 2,
  "disk_low": false,
  "last_attempt_at_ms": 1700000100000,
  "last_success_at_ms": 1700000095000,
  "estimated_lag_ms": 5000,
  "counters": {
    "blocks_downloaded_total": 12,
    "blocks_merged_total": 12,
    "blocks_skipped_total": 40,
    "sync_errors_total": 1,
    "divergence_total": 2
  }
}
```

### Пример

```bash
curl "http://127.0.0.1:8080/sync/status" \
  -H "Authorization: Bearer <sync-token>" \
  -H "X-DFH-Timestamp: 1700000000000" \
  -H "X-DFH-Nonce: a1b2c3d4e5f67890" \
  -H "X-DFH-Signature: <hmac_sha256_hex>"
```

## Конфигурация

### Секция `sync`

```json
"sync": {
  "enabled": true,
  "pull_interval_ms": 60000,
  "request_timeout_ms": 30000,
  "meta_max_blocks": 10000,
  "max_blocks_per_cycle": 1000,
  "max_parallel_downloads": 4,
  "outbound_token": "sync-outbound-token"
}
```

Поля и значения по умолчанию:
- `enabled`: `false`
- `pull_interval_ms`: `60000`
- `request_timeout_ms`: `30000`
- `meta_max_blocks`: `10000`
- `max_blocks_per_cycle`: `1000`
- `max_parallel_downloads`: `4`
- `outbound_token`: пустая строка

### Секция `peers`

```json
"peers": [
  {
    "id": "node-b",
    "url": "http://192.168.1.2:8080"
  }
]
```

Поля:
- `id`: стабильный идентификатор peer-ноды
- `url`: базовый URL peer-ноды, должен начинаться с `http://` или `https://`

## Freshness-правило
- Если `peer.last_ts > local.last_ts`, peer считается новее.
- Если `last_ts` равны, но `peer.record_count > local.record_count`, peer считается новее.
- Если `last_ts` и `record_count` равны, но `hash` различается, это divergence.

## Divergence policy
- Нода пишет `WARN`.
- Увеличивается `divergence_total`.
- Блок скачивается заново и merge'ится.
- В Sync v1 authoritative-источником считается peer.

## Pull loop
Алгоритм цикла:
1. Обновить `last_attempt_at_ms`.
2. Для каждого peer загрузить `/sync/meta`.
3. Построить diff по freshness/divergence-правилу.
4. Для каждого выбранного блока скачать `/sync/block`.
5. Применить `merge_block_dfhbin()` локального адаптера.
6. Если хотя бы один peer успешно опрошен, обновить `last_success_at_ms`.

Поведение при `disk_low`:
- входящие `GET /sync/block` и `POST /sync/meta` продолжают отвечать;
- pull loop пропускает блок целиком до download;
- увеличивается `sync_errors_total`.

## Security
- Исходящие запросы к peer'ам подписываются тем же anti-replay протоколом, что и входящие.
- `PeerSyncService::add_auth_headers()` добавляет:
  - `Authorization`
  - `X-DFH-Timestamp`
  - `X-DFH-Nonce`
  - `X-DFH-Signature`
- `nonce` генерируется как 8 случайных байт в lowercase hex.
- `body_hash` для `GET` вычисляется как SHA-256 от пустого тела.

## Таблица error-кодов
| HTTP | error | Где возникает |
| --- | --- | --- |
| 400 | `missing_anti_replay_headers` | Отсутствуют или неполные anti-replay headers |
| 400 | `invalid_query_param` | Ошибка query/body DTO |
| 401 | `anti_replay_failed` | Подпись, nonce или окно времени не прошли проверку |
| 401 | `unauthorized` | Нет токена или токен невалиден |
| 403 | `forbidden` | Недостаточно scope |
| 404 | `not_found` | Блок не найден |
| 500 | `internal_error` | Внутренняя ошибка sync-обработчика |

## Operational notes
- `disk_low` не блокирует `/sync/meta` и `/sync/block`.
- `SyncRouter` активен независимо от `sync.enabled`.
- `last_success_at_ms` обновляется только если за цикл был хотя бы один успешный peer.
- `estimated_lag_ms = now_epoch_ms() - last_success_at_ms`, либо `0`, если успехов ещё не было.
