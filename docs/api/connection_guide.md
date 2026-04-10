# Connection Guide

Краткое руководство для клиента, который подключается к `dfh-node` без чтения исходного кода.

## Быстрый старт

1. Получите токен с нужным scope: либо из bootstrap-конфига, либо через `Admin API`.
2. Выберите transport:
   HTTP для batch ingest и history, WS для двустороннего обмена и `dfhbin`.
3. Добавьте `Authorization: Bearer <token>` во все запросы и upgrade.
4. Для `write`, `admin`, `sync` обязательно добавьте anti-replay подпись.
5. Настройте retry/backoff и разбиение history-запросов по `history_max_range_ms`.

## Аутентификация

Токен можно получить двумя способами:
- bootstrap-ключ из `config.json`, если клиент и нода управляются вместе;
- динамический ключ через `POST /v1/admin/keys`, если нужен runtime CRUD.

Формат авторизации везде одинаковый:

```http
Authorization: Bearer <token>
```

### Scope matrix

| Endpoint / операция | Scope |
| --- | --- |
| `GET /v1/history`, `GET /v1/status` | `read` |
| `POST /v1/ingest`, WS `op=ingest` | `write` |
| `/v1/admin/keys*` | `admin` |
| `/sync/*` | `sync` |
| `admin` как override для `/sync/*` | допустим |

## Anti-replay

По умолчанию anti-replay обязателен для `write`, `admin`, `sync`.

Алгоритм:
1. Вычислить `signing_key = SHA256(token)` в raw-виде.
2. Сгенерировать `timestamp` в Unix epoch ms.
3. Сгенерировать `nonce` как 8 случайных байт в `hex lowercase` (`16` символов).
4. Собрать каноническую строку.
5. Вычислить `HMAC-SHA256(signing_key, canonical_string)` и отправить hex lowercase.

### HTTP canonical string

```text
METHOD\n
PATH\n
QUERY_STRING\n
TIMESTAMP\n
NONCE\n
BODY_HASH
```

### WS canonical string

```text
ENDPOINT\n
OP\n
MSG_ID\n
TIMESTAMP\n
NONCE\n
PAYLOAD_HASH
```

### Готовый пример с `openssl`

```bash
TOKEN='test-token'
SIGNING_KEY_HEX=$(printf '%s' "$TOKEN" | openssl dgst -sha256 -binary | xxd -p -c 256)
CANONICAL='POST
/v1/ingest
exchange=binance&order=%20asc&symbol=BTC%2FUSD
1700000000123
0011223344556677
64c355dc41f90aa6c171ea306bfd2e8433c657727c6d088814368cea30ec3b07'
printf '%s' "$CANONICAL" > canonical.txt
openssl dgst -sha256 -mac HMAC -macopt hexkey:$SIGNING_KEY_HEX canonical.txt
```

Ожидаемая подпись для примера выше:
`c8230057960cb068e134510bf60dbb3b2ab47fbe614678533530c8c587cd6dec`

## Rate limits

Дефолтные лимиты из текущего конфига:
- `auth.rps_limit = 100`
- `auth.ws_max_connections = 10`
- `ws.max_ws_connections_total = 1000`

У отдельного ключа лимиты могут быть переопределены в `Admin API`.

При `429`:
- используйте `exponential backoff`;
- добавляйте случайный `jitter`;
- не открывайте новый WS connection loop без паузы, если уже получили `connection_limited`.

## Retry стратегия

Ретраить стоит:
- `429 rate_limited`
- `503 queue_full`
- `504 timeout`
- `507 disk_low` только после повторной проверки состояния ноды

Не ретраить без исправления входных данных:
- `400`
- `401`
- `403`
- `404`
- `409`
- `413`

## Paging / chunking для history

Ограничения по умолчанию:
- `http.history_max_range_ms = 86400000`
- `ws.history_max_range_ms = 86400000`
- `http.history_max_bytes = 104857600`
- `ws.history_max_bytes = 104857600`

Практика клиента:
1. Делите большой диапазон на чанки не больше `history_max_range_ms`.
2. Идите слева направо: `[from, min(from + chunk, to))`.
3. Если получили `response_too_large`, уменьшите размер чанка по времени.

## WS connection lifecycle

Поток работы:
1. Клиент открывает `/ws/json` или `/ws/msgpack`.
2. На upgrade передаёт `Authorization`.
3. Anti-replay headers в HTTP upgrade не требуются; после успешного upgrade anti-replay передаётся уже внутри control-message.
4. На каждый запрос получает ответ с тем же `msg_id`.

### `dfhbin` 2-step flow

1. Отправьте control-message `op=ingest` без `payload_base64`, но с `payload_sha256`.
2. Сразу после этого отправьте binary frame с raw `dfhbin`.
3. Сервер сверит `sha256(binary_frame)` с `payload_sha256`.
4. При несовпадении придёт `sha256_mismatch`.
5. При разрыве соединения повторите оба шага заново на новом соединении.

### Переподключение

- При close code `1008` повторное подключение имеет смысл только после исправления токена/scope/лимитов.
- При сетевом разрыве повторно открывайте соединение с `backoff`.
- Pending `dfhbin` state не переносится между соединениями.

## Disk low

- HTTP write-операции возвращают `507 disk_low`.
- WS write-операции возвращают JSON/MessagePack response с `error_code: "disk_low"`.
- `history` и sync read-path продолжают работать.

Рекомендация клиенту:
- перед повторной записью опрашивайте `/v1/status`;
- ориентируйтесь на поле `disk_low`;
- не спамьте retry, пока `disk_low=true`.
