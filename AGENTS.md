АГЕНТЫ

Документ для разработчиков и ИИ-агентов по работе с dfh-node. Цель — собрать в одном месте
актуальные правила по структуре, сборке, тестам, ограничениям и процессу разработки.
Все формулировки технические, примеры и команды — реальные из репозитория.

## 0. Назначение документа
- Этот документ описывает правила разработки для dfh-node (отдельный репозиторий, не DataFeedHub).
- Используется как единый справочник для людей и ИИ-агентов: архитектура, сборка, тесты, запреты.

## 1. Краткое описание проекта
- dfh-node — нода для хранения и выдачи истории рыночных данных, ingest по HTTP/WS, sync между нодами.
- Репозиторий самостоятельный: сетевой слой и инфраструктура вокруг DFH engine.
- MVP: хранение истории + обязательный ingest по WebSocket; HTTP ingest и history — базовые API.

## 2. Архитектурные границы и слойность (DDD-стиль)
- transport (HTTP/WS) ≠ core очередей ≠ auth/security ≠ DFH adapter.
- Нода не лезет “под капот” KV-хранилища DFH: взаимодействие только через IDfhAdapter (контракт).
- Транспорт не знает о форматах хранения — только DTO и протокольные форматы (json/msgpack/dfhbin).
- Авторизация/лимиты не тянут networking и storage напрямую; общение через сервисы/интерфейсы.
- Коллбеки роутера: максимум 1–3 аргумента; сложные параметры передаются DTO через unique_ptr.

## 2.1 Архитектура очередей и TaskScheduler
- BoundedQueue — internal API, простой bounded-контейнер без mutex/cv/shutdown.
- Терминология планировщика: `TaskLane` = приоритет/очередь (`High`/`Low`), `TaskKind` = семантика операции (`Ingest`/`History`), `Task` хранит оба поля.
- TaskScheduler — единый mutex+cv для high/low priority очередей, приоритет high > low, stop-now shutdown.
- WorkerPool — сбор per-lane метрик (total_processed, avg_wait_ms), steady_clock, C++17 fetch_add.
- dfh_node_app — по умолчанию one-shot bootstrap; с флагом `--run` запускает HTTP+WS runtime.

## 3. Зафиксированные спорные/важные тех-решения
- Язык: C++17 (пока).
- HTTP: Simple-Web-Server (подключен); WS: Simple-WebSocket-Server (подключен).
- Boost включаем (часть зависимостей тянет Boost; минимизировать компоненты).
- OpenSSL используем для HMAC/хэшей.
- TLS внутри ноды не делаем: TLS завершается на внешнем nginx/прокси.
- WS форматы: /ws/msgpack (основной) и /ws/json (fallback).
- dfhbin передается “как есть”: по WS — binary frame, по HTTP — тело запроса/ответа.
- Для dfhbin-операций используем ts (метка блока), не from/to.
- Перегруз: history режется первой; ingest держим максимально живым (HTTP reject, WS drop + error по msg_id).
- Anti-replay (ts + nonce + HMAC) внедряем сразу.
- Для WS действуют отдельные operational limits: `history_max_range_ms`, `history_max_bytes`, `max_ws_connections_total`, `request_timeout_ms`.
- `/v1/status` и `/metrics` публикуют queue/disk показатели, gate anomaly counters и `ws_active_connections_total`.

## 3.1 Code style
- Отступы: 4 пробела (без табов) для C/C++ и CMake.
- `public:` / `protected:` / `private:` внутри классов идут без дополнительного отступа (на уровне `class`).
- Следовать .editorconfig.
- Перед коммитом запускать clang-format для измененных C/C++ файлов.
- Приватные поля классов именуем с префиксом `m_` (например, `m_scheduler`, `m_mutex`).
- Для accessor-методов используем имена без префикса `get_` (`total_processed()`, `avg_wait_ms()`, `high_metrics()`).
- Заголовки `.hpp` размещаем рядом с реализациями `.cpp` в `src/`; отдельную папку `include/` не используем.
- Для внешних потребителей библиотеки (`src/app`, `tests`, примеры) используем umbrella-заголовки
  `core.hpp`, `config.hpp`, `security.hpp`, `auth.hpp`, `scheduler.hpp`, `adapter.hpp`, `transport.hpp`, `sync.hpp`
  как приоритетный способ подключения.
- Прямые include вида `core/...`, `config/...`, `security/...`, `auth/...`, `scheduler/...`, `adapter/...`
  допускаются только когда umbrella не покрывает нужный API (например, internal API
  `scheduler/internal/bounded_queue.hpp`).
- Файлы `.ipp` используем только для шаблонного кода (templates); для обычного кода используем `.hpp` + `.cpp`.
- Header-only допускается только когда это оправдано шаблонами/инлайном; нетемплейтные реализации выносим в `.cpp`.

## 3.2 Logging
- Используем log-it-cpp напрямую: LOGIT_TRACE/DEBUG/INFO/WARN/ERROR/FATAL (или DFH_* алиасы).
- Не добавляем функции-обёртки с va_list/vformat вокруг log-it-cpp.
- Для printf-style используем LOGIT_PRINTF_* / LOGIT_FORMAT_* (или DFH_PRINTF_* / DFH_FORMAT_* алиасы).
- Причина: compile-time gating уровней и отсутствие лишнего форматирования при отключенном уровне.

## 3.3 Комментарии и документация
- Любой новый/изменённый публичный API обязан иметь Doxygen-комментарий (/// с \brief/\param/\return и т.п.).
- Для Doxygen используем формы с обратным слешем (\brief, \param, \return, \note).
- Стиль Doxygen-комментариев: только `///`; блочные формы `/** ... */` не использовать.
- Любая нетривиальная логика обязана иметь комментарий "почему так", а не пересказ кода.
- Запрещены бессмысленные комментарии, дублирующие код или имена переменных.
- Все комментарии пишем по-русски (Doxygen-теги допускаются).
- Английский в комментариях допускается только как точное имя сущности из кода/протокола:
  имена типов/классов/методов/полей/enum, макросы, endpoint/header, значения протокола.
- Запрещено писать английские слова как обычный текст комментария, если это не идентификатор.
- Рекомендуется оформлять такие вкрапления в backticks: `TaskScheduler`, `payload_hash`, `X-DFH-Nonce`.
- Примеры:
  - правильно: `Проверяем поле payload_hash перед вызовом verify_signature().`
  - неправильно: `Проверяем payload hash before signature verify.`
- В каждом новом/изменённом файле должен быть файл-комментарий (\file/\brief/\details), написанный вручную в рамках проекта (не из сторонних библиотек).

## 4. Структура репозитория (фактическая)
- docs/ — документация и правила (в т.ч. third_party).
- src/dfh_node/ — библиотека ноды: `.cpp` и соответствующие `.hpp` рядом, основные подкаталоги:
  `adapter/`, `auth/`, `config/`, `core/`, `scheduler/`, `security/`, `sync/`, `transport/http/`,
  а также umbrella-заголовки `core.hpp`, `config.hpp`, `security.hpp`, `auth.hpp`,
  `scheduler.hpp`, `adapter.hpp`, `transport.hpp`, `sync.hpp`.
- src/app/ — приложение: `main.cpp`.
- tests/ — тесты: smoke + config + scheduler/worker + auth/rate-limit/gate + anti-replay + adapter + transport/http + transport/ws
  + sync
  (`test_smoke.cpp`, `test_config_defaults.cpp`, `test_config_loader.cpp`,
  `test_config_validator.cpp`, `test_task_scheduler.cpp`, `test_worker_pool.cpp`,
  `test_bounded_queue.cpp`, `test_scope.cpp`, `test_scope_auth.cpp`,
  `test_fingerprint_computer.cpp`, `test_config_api_key_store.cpp`,
  `test_disk_monitor.cpp`, `test_mdbx_api_key_store.cpp`, `test_composite_api_key_store.cpp`,
  `test_api_key_manager.cpp`, `test_auth_cache.cpp`, `test_rate_limiter.cpp`, `test_ws_connection_limiter.cpp`,
  `test_auth_service.cpp`, `test_unified_gate.cpp`, `test_sha256_utils.cpp`,
  `test_canonical_request.cpp`, `test_nonce_store.cpp`, `test_anti_replay_validator.cpp`,
  `test_gate_e2e.cpp`, `test_status.cpp`, `test_dfh_adapter_dto.cpp`,
  `test_fake_dfh_adapter.cpp`, `test_dfh_adapter_e2e.cpp`,
  `test_http_error_map.cpp`, `test_http_dto_parser.cpp`, `test_http_reply_handle.cpp`,
  `test_http_integration.cpp`, `test_admin_router.cpp`, `test_ops_endpoints.cpp`,
  `test_sync_dto_parser.cpp`, `test_peer_sync_service.cpp`, `test_sync_router.cpp`, `test_sync_e2e.cpp`,
  `test_ws_protocol.cpp`, `test_ws_session_registry.cpp`,
  `test_ws_dto_parser.cpp`, `test_ws_integration.cpp`, `test_ws_runtime_components.cpp`,
  `test_disk_low.cpp`).
- tests/app_configs/ — фикстуры конфигов для CTest-сценариев приложения.
- examples/ — примеры: `config_minimal.json`.
- third_party/ — каталог для submodules (см. docs/third_party.md).
- cmake/ — CMake-скрипты (Options/Warnings/ThirdParty).

### 4.1 Таргеты CMake
- Библиотека: `dfh_node` (STATIC).
- Приложение: `dfh_node_app`.
- Тесты (CTest): `test_tests_registry_consistency`, `test_comment_style`,
  `dfh_node_smoke`, `app_no_args`, `app_valid_config`, `app_missing_config`,
  `app_config_without_value`, `app_invalid_validation_config`,
  `app_valid_config_trace`, `app_valid_config_debug`,
  `app_valid_config_warn_upper`, `app_valid_config_error`,
  `test_config_defaults`, `test_config_loader`, `test_config_validator`,
  `test_task_scheduler`, `test_worker_pool`, `test_bounded_queue`, `test_scope`,
  `test_scope_auth`, `test_fingerprint_computer`, `test_config_api_key_store`,
  `test_disk_monitor`, `test_mdbx_api_key_store`, `test_composite_api_key_store`,
  `test_api_key_manager`, `test_auth_cache`, `test_rate_limiter`, `test_ws_connection_limiter`,
  `test_auth_service`, `test_unified_gate`, `test_sha256_utils`,
  `test_canonical_request`, `test_nonce_store`, `test_anti_replay_validator`,
  `test_gate_e2e`, `test_status`, `test_dfh_adapter_dto`, `test_fake_dfh_adapter`,
  `test_dfh_adapter_e2e`, `test_http_error_map`, `test_http_dto_parser`,
  `test_http_reply_handle`, `test_http_integration`, `test_admin_router`,
  `test_ops_endpoints`, `test_sync_dto_parser`, `test_peer_sync_service`,
  `test_sync_router`, `test_sync_e2e`, `test_ws_protocol`, `test_ws_session_registry`,
  `test_ws_dto_parser`, `test_ws_integration`, `test_ws_runtime_components`,
  `test_disk_low`.

### 4.2 Опции CMake (реальные)
- `DFH_NODE_BUILD_TESTS` (ON) — включить тесты.
- `DFH_NODE_BUILD_EXAMPLES` (ON) — включить примеры.
- `DFH_NODE_USE_SYSTEM_DEPS` (OFF) — использовать системные зависимости вместо third_party.
- `DFH_NODE_ENABLE_TLS` (OFF) — включить TLS поддержку (по политике проекта — оставлять OFF).

## 5. Сборка и тесты (Windows: MSVC + MinGW)
### 5.1 MSVC
- Конфигурация (без явного генератора):
  - `cmake -S . -B build-msvc`
- Сборка:
  - `cmake --build build-msvc --config Debug`
- Тесты (из каталога `build-msvc`):
  - `ctest -C Debug --output-on-failure`
- Скрипт “единый прогон”: `run-all-tests.bat`.
  - Использует `DFH_MSVC_GENERATOR` и `DFH_MSVC_ARCH`, если заданы.

### 5.2 MinGW
- Скрипт сборки/тестов: `build-tests-mingw.bat`.
  - Генератор: `MinGW Makefiles`.
  - Включает `DFH_NODE_BUILD_TESTS=ON`.

### 5.3 Единый прогон (MSVC + MinGW)
- `run-all-tests.bat`:
  1) Конфигурирует/собирает `build-msvc`.
  2) Запускает `ctest -C Debug --output-on-failure`.
  3) Вызывает `build-tests-mingw.bat`.

### 5.4 Тесты
- Сейчас включены тесты (полный список см. раздел 4.1), включая:
  smoke/стиль/регистрацию (`test_tests_registry_consistency`, `test_comment_style`, `dfh_node_smoke`, `app_*`),
  core/config/scheduler (`test_config_*`, `test_task_scheduler`, `test_worker_pool`, `test_bounded_queue`,
  `test_status`, `test_disk_monitor`, `test_mdbx_api_key_store`, `test_composite_api_key_store`, `test_api_key_manager`),
  auth/security (`test_scope*`, `test_fingerprint_computer`, `test_auth_cache`, `test_auth_service`,
  `test_rate_limiter`, `test_ws_connection_limiter`, `test_unified_gate`, `test_sha256_utils`,
  `test_canonical_request`, `test_nonce_store`, `test_anti_replay_validator`, `test_gate_e2e`),
  adapter (`test_dfh_adapter_dto`, `test_fake_dfh_adapter`, `test_dfh_adapter_e2e`),
  transport/http (`test_http_error_map`, `test_http_dto_parser`, `test_http_reply_handle`, `test_http_integration`,
  `test_admin_router`, `test_ops_endpoints`),
  sync (`test_sync_dto_parser`, `test_peer_sync_service`, `test_sync_router`, `test_sync_e2e`),
  transport/ws (`test_ws_protocol`, `test_ws_session_registry`, `test_ws_dto_parser`,
  `test_ws_integration`, `test_ws_runtime_components`, `test_disk_low`).
- Если тестов недостаточно — добавляйте новые и регистрируйте через `add_test`.

## 6. Процесс разработки
- Ветки: фича/фикс — отдельная ветка от `main`, PR в `main`.
- Коммиты: осмысленные, один логический шаг — один коммит.
- Не коммитить артефакты: `build-*`, `.vs/`, `.vscode/`, `out/`, `cmake-build-*`, `.sln/.vcxproj`.
- Перед PR: сборка и тесты MSVC + MinGW (если поддерживается на машине).

## 7. Зависимости
- `third_party/` — место под git submodules; в репозитории есть `.gitmodules`.
- Правила обновления зависимостей: `docs/third_party.md`.
- Изменения зависимостей — отдельная задача/PR.
- Подключенные deps:
  - nlohmann/json (JSON).
  - log-it-cpp (logging).
  - OpenSSL (HMAC/хэши, token wipe через `OPENSSL_cleanse`).
  - Simple-Web-Server + Asio (HTTP transport runtime).
  - Simple-WebSocket-Server (WS transport runtime).
  - msgpack-c (контрольные сообщения WS `/ws/msgpack`).
- Планируемые ключевые deps (TODO до подключения):
  - Boost (минимальные компоненты, как fallback при сборке без standalone Asio).

## 8. Протоколы и API (краткая справка, без кода)
- HTTP (реализовано в runtime при запуске `dfh_node_app --run`):
  - `/v1/history` — выгрузка истории.
  - `/v1/ingest` — прием новых данных.
  - `/v1/status` — статус ноды.
- Admin/Ops (реализовано в runtime при запуске `dfh_node_app --run`):
  - `/v1/admin/keys` — CRUD для динамических API-ключей в `MDBX`.
  - `/health`, `/ready`, `/metrics` — эксплуатационные endpoints.
- Sync (реализовано в runtime при запуске `dfh_node_app --run`):
  - `/sync/meta` — список метаданных блоков peer-ноды.
  - `/sync/block` — выдача raw `dfhbin` блока по ключу.
  - `/sync/status` — состояние pull loop и sync-счётчиков.
- WS (реализовано в runtime при запуске `dfh_node_app --run` и `ws.port != 0`):
  - Endpoints: `/ws/msgpack` (основной), `/ws/json` (fallback).
  - Control-message: `op=ingest|history|subscribe` + `msg_id`.
  - dfhbin: binary frames.

## 9. Security
- Scopes: `read` / `write` / `admin` / `sync`.
- Fingerprint: `HMAC(server_secret, token)`; plaintext токены не хранить.
- Auth cache: TTL 30–60s + invalidation по `updated_at`/`revoked_at`.
- Rate limit: req/sec + max ws connections.
- Anti-replay: canonical string, окно времени, nonce store (TTL/LRU).

### 9.5 Протокол anti-replay

**Ключ подписи:** `signing_key = SHA256(token)` (32 сырых байта)
- HTTP: вычисляется на лету из заголовка Authorization
- WS: вычисляется при handshake/upgrade, хранится в WsConnectionContext (32 байта)
- Plaintext token НЕ хранится долго (требование этапа 4)

**HTTP хедеры:**
- `Authorization: Bearer <token>` (как в этапе 4)
- `X-DFH-Timestamp` (Unix epoch ms, 13 цифр)
- `X-DFH-Nonce` (hex lowercase, 16 символов = 8 случайных байт)
- `X-DFH-Signature` (HMAC-SHA256 в hex lowercase, 64 символа)

**Поля WS control-message:**
- `timestamp` (Unix epoch ms, 13 цифр)
- `nonce` (hex lowercase, 16 символов)
- `signature` (HMAC-SHA256 в hex lowercase, 64 символа)
- `op` (операция: ingest/history/subscribe)
- `msg_id` (идентификатор сообщения)
- `payload_hash` (SHA-256 payload в hex lowercase, 64 символа)
- `payload_sha256` (для dfhbin: SHA-256 бинарного кадра в hex, 64 символа)
- `endpoint` для подписи берется сервером из `WsConnectionContext::endpoint`, а не из control-message.

**Канонический формат HTTP:**
```
METHOD\n
PATH\n
QUERY_STRING\n
TIMESTAMP\n
NONCE\n
BODY_HASH
```
(Последний `\n` НЕ включается)

**Канонический формат WS:**
```
ENDPOINT\n
OP\n
MSG_ID\n
TIMESTAMP\n
NONCE\n
PAYLOAD_HASH
```
(Последний `\n` НЕ включается)

**Порядок проверок:**
1. Parse (проверка формата: timestamp число, nonce 16 hex, signature 64 hex, signing_key 32 bytes)
2. Timestamp skew: `abs(now_ms - request_ts) <= max_skew_ms` (разрешает будущее в пределах окна)
3. Проверка подписи: `HMAC-SHA256(signing_key, canonical_string) == signature` (constant-time compare)
4. Уникальность nonce: `NonceStore.check_and_record(fingerprint, nonce, server_now)` (TTL от server_now)

**Политика require_for_scopes:**
- По умолчанию: write/admin/sync обязательны
- Если disabled + required scope → REJECT с AntiReplayRequired (НЕ skip)

**Формат nonce:** hex lowercase, 16 символов (8 случайных байт, crypto.randomBytes)

**Правило capacity:** `nonce_capacity >= peak_rps_per_fingerprint * nonce_ttl_seconds * 1.5` (запас 50%)

**Хеш пустого тела:** `sha256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`

**WS payload_hash:**
- dfhbin: sha256(binary_frame_bytes)
- json/msgpack: sha256(payload_bytes_raw) (как передано по сети, ДО парсинга)
- history/subscribe: sha256("") (нет payload)

**Канонизация query-параметров:**
- Декодировать URL-encoding (`+` → пробел, `%2F` → `/`)
- Отсортировать по (key, value) в лексикографическом порядке
- Канонически перекодировать (RFC3986: пробел → `%20`, НЕ `+`)

## 10. Sync v1
- Pull-модель, peers статически в конфиге.
- Diff по meta/hash/revision: сначала `end_ts`, затем `count`.
- Eventual consistency как базовая модель.
- `SyncRouter` регистрируется всегда; входящие `/sync/*` доступны независимо от `sync.enabled`.
- `PeerSyncService` запускается только при `sync.enabled = true` и непустом `peers`.
- Все `/sync/*` требуют anti-replay, включая `GET`.
- `disk_low` не блокирует `/sync/meta` и `/sync/block`, но в pull loop блок скачивания пропускается и растёт `sync_errors_total`.
- Merge downloaded блоков выполняется только через `merge_block_dfhbin()`, не через `ingest_structured()`.

## 10.1 Правила Mdbx
- `MdbxApiKeyStore` отвечает только за открытие `MDBX`, чтение и атомарную запись/удаление во всех трёх индексах:
  `keys_by_id`, `keys_by_fingerprint`, `keys_by_name`.
- Все мутации ключей выполняются только через `ApiKeyManager`; после каждой мутации обязателен
  `auth_cache.invalidate(fingerprint)`.
- `DiskMonitor::is_disk_low()` вызывать только из hot path transport-слоя; внутри есть кэш 5 секунд, но вызов всё равно
  синхронизируется через mutex.
- Admin операции выполняются синхронно и не отправляются в `TaskScheduler`.
- Bootstrap-ключи из конфига считаются read-only; Admin CRUD работает только с `MDBX` и для config-ключей
  эквивалентен `404`.

## 11. Чек-лист перед коммитом
- Сборка `build-msvc` и тесты `ctest -C Debug --output-on-failure`.
- Прогон `build-tests-mingw.bat` (если MinGW доступен).
- Прогнан `clang-format` для всех изменённых C/C++ файлов (`*.cpp`, `*.hpp`).
- Обновлены документы/README при изменении API/поведения.
- Добавлены комментарии к новым/изменённым сущностям?
- Нет комментариев, повторяющих код?
- Нет английского «прозы» в комментариях (кроме точных имен полей/типов/API)?
- Нет артефактов сборки в git status.

## 12. Быстрые команды
```bat
cmake -S . -B build-msvc
cmake --build build-msvc --config Debug
ctest -C Debug --output-on-failure
```

```bat
build-tests-mingw.bat
```

```bat
run-all-tests.bat
```
