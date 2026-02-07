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
- dfh_node_app — one-shot mode до появления HTTP/WS транспорта (логирование и немедленный выход).

## 3. Зафиксированные спорные/важные тех-решения
- Язык: C++17 (пока).
- HTTP: Simple-Web-Server; WS: Simple-WebSocket-Server (планируемые зависимости).
- Boost включаем (часть зависимостей тянет Boost; минимизировать компоненты).
- OpenSSL используем для HMAC/хэшей.
- TLS внутри ноды не делаем: TLS завершается на внешнем nginx/прокси.
- WS форматы: /ws/msgpack (основной) и /ws/json (fallback).
- dfhbin передается “как есть”: по WS — binary frame, по HTTP — тело запроса/ответа.
- Для dfhbin-операций используем ts (метка блока), не from/to.
- Перегруз: history режется первой; ingest держим максимально живым (HTTP reject, WS drop + error по msg_id).
- Anti-replay (ts + nonce + HMAC) внедряем сразу.

## 3.1 Code style
- Отступы: 4 пробела (без табов) для C/C++ и CMake.
- Следовать .editorconfig.
- Перед коммитом запускать clang-format для измененных C/C++ файлов.
- Приватные поля классов именуем с префиксом `m_` (например, `m_scheduler`, `m_mutex`).
- Для accessor-методов используем имена без префикса `get_` (`total_processed()`, `avg_wait_ms()`, `high_metrics()`).
- Заголовки `.hpp` размещаем рядом с реализациями `.cpp` в `src/`; отдельную папку `include/` не используем.
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
- Любая нетривиальная логика обязана иметь комментарий "почему так", а не пересказ кода.
- Запрещены бессмысленные комментарии, дублирующие код или имена переменных.
- Все комментарии пишем по-русски (Doxygen-теги допускаются).
- В каждом новом/изменённом файле должен быть файл-комментарий (\file/\brief/\details), написанный вручную в рамках проекта (не из сторонних библиотек).

## 4. Структура репозитория (фактическая)
- docs/ — документация и правила (в т.ч. third_party).
- src/dfh_node/ — библиотека ноды: `.cpp` и соответствующие `.hpp` рядом
  (`version.*`, `config.*`, `config_loader.*`, `config_validator.*`, `scope.hpp`,
  `fingerprint_computer.*`, `api_key_store.hpp`, `config_api_key_store.*`,
  `auth_cache.*`, `rate_limiter.*`, `ws_connection_limiter.*`, `auth_service.*`,
  `unified_gate.*`, `status.*`, `task.*`, `task_scheduler.*`, `worker_pool.*`,
  `internal/bounded_queue.hpp`, `logging.hpp`).
- src/app/ — приложение: `main.cpp`.
- tests/ — тесты: smoke + config + scheduler/worker + auth/rate-limit/gate
  (`test_smoke.cpp`, `test_config_defaults.cpp`, `test_config_loader.cpp`,
  `test_config_validator.cpp`, `test_task_scheduler.cpp`, `test_worker_pool.cpp`,
  `test_scope.cpp`, `test_fingerprint_computer.cpp`, `test_auth_cache.cpp`,
  `test_rate_limiter.cpp`, `test_ws_connection_limiter.cpp`,
  `test_auth_service.cpp`, `test_unified_gate.cpp`, `test_gate_e2e.cpp`).
- examples/ — примеры: `config_minimal.json`.
- third_party/ — каталог для submodules (см. docs/third_party.md).
- cmake/ — CMake-скрипты (Options/Warnings/ThirdParty).

### 4.1 Таргеты CMake
- Библиотека: `dfh_node` (STATIC).
- Приложение: `dfh_node_app`.
- Тесты (CTest): `dfh_node_smoke`, `test_config_defaults`, `test_config_loader`,
  `test_config_validator`, `test_task_scheduler`, `test_worker_pool`, `test_scope`,
  `test_fingerprint_computer`, `test_auth_cache`, `test_rate_limiter`,
  `test_ws_connection_limiter`, `test_auth_service`, `test_unified_gate`,
  `test_gate_e2e`, а также smoke-тесты `dfh_node_app`.

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
- Сейчас включены тесты: `dfh_node_smoke`, `test_config_defaults`,
  `test_config_loader`, `test_config_validator`, `test_task_scheduler`,
  `test_worker_pool`, `test_scope`, `test_fingerprint_computer`,
  `test_auth_cache`, `test_rate_limiter`, `test_ws_connection_limiter`,
  `test_auth_service`, `test_unified_gate`, `test_gate_e2e`,
  а также smoke-тесты приложения через CTest.
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
- Планируемые ключевые deps (TODO до подключения):
  - Simple-Web-Server (HTTP), Simple-WebSocket-Server (WS).
  - Boost (минимальные компоненты).
  - MessagePack (контрольные сообщения в WS).

## 8. Протоколы и API (краткая справка, без кода)
- HTTP (планируемые пути, TODO до внедрения):
  - `/v1/history` — выгрузка истории.
  - `/v1/ingest` — прием новых данных.
  - `/v1/status` — статус ноды.
- WS:
  - Endpoints: `/ws/msgpack` (основной), `/ws/json` (fallback).
  - Control-message: `op=ingest|history|subscribe` + `msg_id`.
  - dfhbin: binary frames.

## 9. Security
- Scopes: `read` / `write` / `admin` / `sync`.
- Fingerprint: `HMAC(server_secret, token)`; plaintext токены не хранить.
- Auth cache: TTL 30–60s + invalidation по `updated_at`/`revoked_at`.
- Rate limit: req/sec + max ws connections.
- Anti-replay: canonical string, окно времени, nonce store (TTL/LRU).

## 10. Sync v1
- Pull-модель, peers статически в конфиге.
- Diff по meta/hash/revision: сначала `end_ts`, затем `count`.
- Eventual consistency как базовая модель.

## 11. Чек-лист перед коммитом
- Сборка `build-msvc` и тесты `ctest -C Debug --output-on-failure`.
- Прогон `build-tests-mingw.bat` (если MinGW доступен).
- Обновлены документы/README при изменении API/поведения.
- Добавлены комментарии к новым/изменённым сущностям?
- Нет комментариев, повторяющих код?
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
