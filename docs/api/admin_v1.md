# Admin API v1

## Обзор

`Admin API` предоставляет HTTP-интерфейс для управления динамическими API-ключами, которые хранятся в `MDBX`.
Bootstrap-ключи из конфигурации доступны только для runtime-авторизации и не участвуют в Admin CRUD.

## Авторизация

- Используется `Authorization: Bearer <token>`.
- Для всех endpoint'ов нужен `admin` scope.
- Для `POST`, `PUT`, `DELETE` и `revoke` anti-replay обязателен:
  - `X-DFH-Timestamp`
  - `X-DFH-Nonce`
  - `X-DFH-Signature`

## Endpoints

| Метод | Путь | Описание |
| --- | --- | --- |
| `GET` | `/v1/admin/keys` | Список MDBX-ключей; `?status=all` включает revoked |
| `POST` | `/v1/admin/keys` | Создать ключ; plaintext `token` возвращается один раз |
| `GET` | `/v1/admin/keys/{id}` | Получить ключ по UUID |
| `PUT` | `/v1/admin/keys/{id}` | Частично обновить ключ |
| `POST` | `/v1/admin/keys/{id}/revoke` | Отозвать ключ; операция идемпотентна |
| `DELETE` | `/v1/admin/keys/{id}` | Физически удалить ключ из всех индексов |

## Request/Response schemas

### Create request

```json
{
  "name": "writer",
  "scopes": ["read", "write"],
  "rps_limit": 100,
  "ws_max_connections": 2,
  "expires_at_ms": null
}
```

### Update request

```json
{
  "name": "writer-updated",
  "scopes": ["read", "write"],
  "rps_limit": 250,
  "ws_max_connections": 4,
  "expires_at_ms": 1735689600000
}
```

### Key response

```json
{
  "id": "b7d071dd-bce5-4da4-b77a-f293fbf8fa13",
  "name": "writer",
  "fingerprint": "7b1c...",
  "scopes": ["read", "write"],
  "rps_limit": 100,
  "ws_max_connections": 2,
  "expires_at_ms": null,
  "revoked": false,
  "created_at_ms": 1731000000000,
  "updated_at_ms": 1731000000000
}
```

### Create response

```json
{
  "id": "b7d071dd-bce5-4da4-b77a-f293fbf8fa13",
  "name": "writer",
  "fingerprint": "7b1c...",
  "scopes": ["read", "write"],
  "rps_limit": 100,
  "ws_max_connections": 2,
  "expires_at_ms": null,
  "revoked": false,
  "created_at_ms": 1731000000000,
  "updated_at_ms": 1731000000000,
  "token": "6d0f..."
}
```

## Scope и лимиты

- `scopes` — массив строк из множества `read`, `write`, `admin`, `sync`.
- Пустой `scopes` означает отсутствие прав.
- `rps_limit = 0` означает наследование `auth.rps_limit` из конфига.
- `ws_max_connections = 0` означает наследование `auth.ws_max_connections` из конфига.
- `expires_at_ms = null` означает бессрочный ключ.

## HTTP статусы

- `200` — успешное чтение, обновление или revoke.
- `201` — ключ создан.
- `204` — ключ удалён.
- `400` — ошибка формата JSON/DTO или anti-replay.
- `401` — отсутствует или невалиден Bearer token.
- `403` — не хватает `admin` scope.
- `404` — ключ не найден.
- `409` — конфликт уникального имени.
- `507` — операция записи отклонена из-за `disk_low`.

## Коды ошибок

| Код | Статус | Описание |
| --- | --- | --- |
| `disk_low` | `507` | Запись запрещена из-за нехватки диска |
| `duplicate_name` | `409` | Имя ключа уже занято |
| `not_found` | `404` | Ключ не найден |
| `missing_anti_replay_headers` | `400` | Для мутации не хватает anti-replay заголовков |
| `invalid_json` | `400` | Тело запроса не является валидным JSON |
| `invalid_query_param` | `400` | Некорректный формат DTO Admin API |

## Примеры cURL

### Создание ключа

```bash
curl -X POST http://127.0.0.1:8080/v1/admin/keys \
  -H "Authorization: Bearer <admin-token>" \
  -H "X-DFH-Timestamp: <ts_ms>" \
  -H "X-DFH-Nonce: <nonce>" \
  -H "X-DFH-Signature: <signature>" \
  -H "Content-Type: application/json" \
  -d '{"name":"writer","scopes":["read","write"],"rps_limit":100,"ws_max_connections":2}'
```

### Обновление ключа

```bash
curl -X PUT http://127.0.0.1:8080/v1/admin/keys/<id> \
  -H "Authorization: Bearer <admin-token>" \
  -H "X-DFH-Timestamp: <ts_ms>" \
  -H "X-DFH-Nonce: <nonce>" \
  -H "X-DFH-Signature: <signature>" \
  -H "Content-Type: application/json" \
  -d '{"rps_limit":250,"ws_max_connections":4}'
```

### Revoke ключа

```bash
curl -X POST http://127.0.0.1:8080/v1/admin/keys/<id>/revoke \
  -H "Authorization: Bearer <admin-token>" \
  -H "X-DFH-Timestamp: <ts_ms>" \
  -H "X-DFH-Nonce: <nonce>" \
  -H "X-DFH-Signature: <signature>"
```

### Удаление ключа

```bash
curl -X DELETE http://127.0.0.1:8080/v1/admin/keys/<id> \
  -H "Authorization: Bearer <admin-token>" \
  -H "X-DFH-Timestamp: <ts_ms>" \
  -H "X-DFH-Nonce: <nonce>" \
  -H "X-DFH-Signature: <signature>"
```
