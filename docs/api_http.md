# HTTP API (draft)

## Purpose
Этот документ фиксирует контракт HTTP API для выгрузки истории, приема данных, статуса, админки и синхронизации.
Он описывает, какие операции доступны и как клиент с ними взаимодействует, без детализации полей.
Описание ориентировано на согласование поведения до реализации.

## Versioning
API будет версионироваться через префикс пути, например /v1/..., как идея для будущей реализации.

## Content outline
1. Overview
   - transport: HTTP
   - auth: API key + scopes
   - rate limits
2. Authentication & Authorization
   - API key in header ("Authorization" header or "X-API-Key")
   - scopes: read/write/admin/sync
3. Common conventions
   - request/response формат (JSON для control)
   - error model (ok/ignore/error + message)
   - request_id (если используется)
4. Endpoints (MVP list)
   - GET /v1/history
     - цель: скачать историю по (provider, symbol, type, tf, from/to) в CSV или dfhbin
     - лимиты диапазона/размера (конфиг)
   - POST /v1/ingest
     - цель: записать пачку тиков/баров (JSON/MsgPack/dfhbin)
   - GET /v1/status
     - здоровье, версии, очереди, диск, sync summary
5. Admin endpoints (planned)
   - POST/DELETE/GET /v1/admin/keys (CRUD ключей, scopes, exp, лимиты)
6. Sync endpoints (planned)
   - GET /v1/sync/status (ревизии/свежесть)
   - GET /v1/sync/dfhbin?ts=... (получить блок по метке)
7. Error codes (draft)
   - unauthorized/forbidden
   - rate limited
   - queue full / server busy
   - invalid request
   - storage/merge error

## Open questions
- Нужен ли request_id в HTTP ответах или это только для WS?
- Какой точный error model будет принят: коды, текст, дополнительные поля?
- Какие лимиты диапазона истории считать дефолтными?
- Какие поля обязаны быть в статусе и admin-ответах?
