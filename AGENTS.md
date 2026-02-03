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

## 4. Структура репозитория (фактическая)
- docs/ — документация и правила (в т.ч. third_party).
- include/ — публичные заголовки: `version.hpp`, `build_info.hpp`, `config.hpp`,
  `config_loader.hpp`, `config_validator.hpp`, `interfaces.hpp`, `logging.hpp`, `status.hpp`.
- src/dfh_node/ — библиотека ноды: `version.cpp`, `config.cpp`, `config_loader.cpp`,
  `config_validator.cpp`, `logging.cpp`, `status.cpp`.
- src/app/ — приложение: `main.cpp`.
- tests/ — тесты: `test_smoke.cpp`, `test_config_defaults.cpp`,
  `test_config_loader.cpp`, `test_config_validator.cpp`.
- examples/ — примеры: `config_minimal.json`.
- third_party/ — каталог для submodules (см. docs/third_party.md).
- cmake/ — CMake-скрипты (Options/Warnings/ThirdParty).

### 4.1 Таргеты CMake
- Библиотека: `dfh_node` (STATIC).
- Приложение: `dfh_node_app`.
- Тесты: `dfh_node_smoke`, `test_config_defaults`, `test_config_loader`, `test_config_validator` (CTest).

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
  `test_config_loader`, `test_config_validator`, а также smoke-тесты приложения через CTest.
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
- Планируемые ключевые deps (TODO до подключения):
  - Simple-Web-Server (HTTP), Simple-WebSocket-Server (WS).
  - OpenSSL (HMAC/хэши).
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
