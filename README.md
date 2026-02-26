# dfh-node

dfh-node — это сервер (“нода”) для хранения и раздачи исторических рыночных данных и приема новых данных по сети.
Нода предоставляет HTTP API и WebSocket API для загрузки истории, записи тиков/баров и синхронизации между нодами.
Внутреннее хранение и слияние данных реализуется в движке DataFeedHub (DFH), а dfh-node — это сетевой слой
(transport, auth, лимиты, очереди).

## Планируемые возможности (MVP)

- HTTP: выгрузка истории (history download) в форматах CSV и dfhbin (сжатый бинарный формат блоков данных).
- HTTP: прием новых данных (ingest) через POST — пачками или одиночными событиями.
- WebSocket: control-протокол для ingest/history/subscribe.
- WebSocket: передача dfhbin как бинарных фреймов.
- Поддержка двух режимов control-сообщений: JSON (простой) и MessagePack (быстрый).
- Очереди планировщика по приоритету: high-priority важнее low-priority; обычно ingest маппится в high, history в low, и при перегрузе low режется первой.
- Авторизация по API ключам со scope-моделью: read / write / admin / sync.
- Rate limiting: лимит запросов/сек и лимит одновременных WS соединений.
- Anti-replay для подписанных запросов (timestamp + nonce + HMAC).
- Синхронизация нод (pull): ноды могут подтягивать недостающие данные у peers для надежности
  и распределения нагрузки. Sync ориентирован на работу через интернет за прокси (TLS делает nginx/внешний сервис).

## Зачем несколько нод?

- Надежность: если одна нода недоступна, другие продолжают обслуживать запросы и хранить данные.
- Восстановление: вернувшаяся нода подтягивает историю у соседей.
- Масштабирование чтения: можно распределять нагрузку выгрузки истории между нодами.

## Архитектура

- dfh-node (этот репозиторий): HTTP/WS транспорт, очередь задач, auth/лимиты, sync, status/admin API.
- DFH engine (внешняя зависимость): хранение данных, слияние (merge), вычисление хэшей блоков,
  доступ к истории по from/to.
- Форматы обмена: JSON/MessagePack для control, dfhbin для быстрых бинарных блоков, CSV для удобного экспорта.

## Структура репозитория

- docs/ — документация (API, конфиг, безопасность).
- src/dfh_node/ — ядро ноды: заголовки `.hpp` лежат рядом с реализациями `.cpp`.
- src/app/ — приложение (main, wiring, CLI, загрузка конфига).
- tests/ — тесты.
- examples/ — примеры клиентов/сценариев.
- third_party/ — зависимости (vendored/submodules).
- cmake/ — CMake helper-скрипты.

## Статус проекта

Репозиторий уже содержит рабочее ядро:

- конфигурация/валидация/загрузка конфига;
- очереди и worker pool (high/low lane);
- auth/rate-limit ядро (scope, fingerprint, auth cache, API key store, unified gate);
- unit/smoke/E2E тесты через CTest.

HTTP/WS транспорт и sync-протокол остаются следующими этапами.

## Быстрый старт

Сборка и запуск в режиме Debug:

```bat
cmake -S . -B build-msvc
cmake --build build-msvc --config Debug
build-msvc\src\app\dfh_node_app.exe --config examples\config_minimal.json
```

Текущие таргеты сборки:

- dfh_node (статическая библиотека)
- dfh_node_app (исполняемый файл)

## Конфигурация

Полный формат `config.json` описан в `docs/config.md`.
Минимальный пример находится в `examples/config_minimal.json`.

## Защита от replay-атак

### Обзор протокола

**Ключ подписи:** `signing_key = SHA256(token)` (32 сырых байта)
- HTTP: вычисляется на лету из заголовка Authorization
- WS: вычисляется при handshake/upgrade, хранится в WsConnectionContext

**HTTP-заголовки:**
- `Authorization: Bearer <token>`
- `X-DFH-Timestamp`: Unix epoch в миллисекундах (13 цифр)
- `X-DFH-Nonce`: hex lowercase (16 символов = 8 случайных байт)
- `X-DFH-Signature`: HMAC-SHA256 в hex (64 символа)

**Поля WS control-message:**
- `timestamp`, `nonce`, `signature`, `endpoint`, `op`, `msg_id`, `payload_sha256`

### Пример клиента (HTTP)

```python
import hashlib
import hmac
import time
import secrets

token = "your-api-token"
method = "POST"
path = "/v1/ingest"
query_string = "exchange=binance&symbol=BTCUSD"
timestamp = str(int(time.time() * 1000))
nonce = secrets.token_hex(8)
body_hash = hashlib.sha256(body_bytes).hexdigest()

# Каноническая строка
canonical = f"{method}\n{path}\n{query_string}\n{timestamp}\n{nonce}\n{body_hash}"

# Ключ подписи
signing_key = hashlib.sha256(token.encode()).digest()

# Подпись
signature = hmac.new(signing_key, canonical.encode(), hashlib.sha256).hexdigest()

# Заголовки
headers = {
    "Authorization": f"Bearer {token}",
    "X-DFH-Timestamp": timestamp,
    "X-DFH-Nonce": nonce,
    "X-DFH-Signature": signature
}
```

### Конфигурация

```json
{
  "security": {
    "anti_replay": {
      "enabled": true,
      "max_skew_ms": 5000,
      "nonce_ttl_ms": 60000,
      "nonce_capacity": 10000,
      "require_for_scopes": ["write", "admin", "sync"]
    }
  }
}
```

**Правило capacity:** `nonce_capacity >= peak_rps_per_fingerprint * nonce_ttl_seconds * 1.5`

## Логирование

Поддерживаемые уровни: `trace`, `debug`, `info`, `warn`, `error`.
Логирование в файл опционально через `logging.file_path`.
Секреты (например, `server_secret`) не выводятся в логах.
