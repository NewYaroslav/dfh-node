# Configuration (JSON)

Документ описывает актуальный формат `config.json`, правила валидации и коды ошибок.

## Минимальный пример
```json
{
  "schema_version": 1,
  "node_id": "node-01",
  "env": "dev",
  "http": {
    "bind_host": "0.0.0.0",
    "port": 8080,
    "max_payload_bytes": 10000000
  },
  "ws": {
    "bind_host": "0.0.0.0",
    "port": 8081,
    "max_payload_bytes": 10000000
  },
  "queues": {
    "ingest_capacity": 10000,
    "history_capacity": 5000,
    "workers": 4
  },
  "security": {
    "server_secret": "test-secret-key-16chars",
    "anti_replay": {
      "enabled": true,
      "max_skew_ms": 5000,
      "nonce_ttl_ms": 60000,
      "nonce_capacity": 10000
    }
  },
  "peers": [],
  "storage": {
    "path": "./data",
    "min_free_bytes": 2000000000
  },
  "logging": {
    "level": "info",
    "console": true,
    "file_path": ""
  }
}
```

## Полный пример
```json
{
  "schema_version": 1,
  "node_id": "node-eu-01",
  "env": "prod",
  "http": {
    "bind_host": "0.0.0.0",
    "port": 8080,
    "max_payload_bytes": 20000000
  },
  "ws": {
    "bind_host": "0.0.0.0",
    "port": 8081,
    "max_payload_bytes": 20000000
  },
  "queues": {
    "ingest_capacity": 20000,
    "history_capacity": 10000,
    "workers": 8
  },
  "security": {
    "server_secret": "prod-secret-key-32chars",
    "anti_replay": {
      "enabled": true,
      "max_skew_ms": 5000,
      "nonce_ttl_ms": 60000,
      "nonce_capacity": 20000
    }
  },
  "peers": [
    { "id": "node-eu-02", "url": "https://peer-02.example.com" },
    { "id": "node-us-01", "url": "https://peer-us.example.com" }
  ],
  "storage": {
    "path": "./data",
    "min_free_bytes": 5000000000
  },
  "logging": {
    "level": "info",
    "console": true,
    "file_path": "./logs/dfh-node.log"
  }
}
```

## Обязательные поля
- `node_id` (string): идентификатор ноды.
- `env` (string): среда выполнения.
- `security.server_secret` (string): секрет для HMAC.

`schema_version` рекомендуется задавать явно. Сейчас поддерживается только значение `1`.

## Секции и поля
### Корень
- `schema_version` (int, default: 1).
- `node_id` (string, required).
- `env` (string, required).
- `http` (object, optional).
- `ws` (object, optional).
- `queues` (object, optional).
- `security` (object, required).
- `peers` (array, optional, default: пустой).
- `storage` (object, optional).
- `logging` (object, optional).

### http
- `bind_host` (string, default: `0.0.0.0`).
- `port` (int, default: `8080`).
- `max_payload_bytes` (int, default: `10000000`).

### ws
- `bind_host` (string, default: `0.0.0.0`).
- `port` (int, default: `8081`).
- `max_payload_bytes` (int, default: `10000000`).

### queues
- `ingest_capacity` (int, default: `10000`).
- `history_capacity` (int, default: `5000`).
- `workers` (int, default: `4`).

### security
- `server_secret` (string, required).
- `anti_replay` (object, optional).

#### security.anti_replay
- `enabled` (bool, default: `true`).
- `max_skew_ms` (int, default: `5000`).
- `nonce_ttl_ms` (int, default: `60000`).
- `nonce_capacity` (int, default: `10000`).

### peers
Массив объектов:
- `id` (string)
- `url` (string)

### storage
- `path` (string, default: `./data`).
- `min_free_bytes` (int, default: `2000000000`).

### logging
- `level` (string, default: `info`).
- `console` (bool, default: `true`).
- `file_path` (string, default: пусто).

## Правила валидации
- `schema_version == 1`
- `node_id`: не пустой, длина <= 64, формат `[A-Za-z0-9_-]+`
- `env`: одно из `dev`, `staging`, `prod`
- `http.port` и `ws.port`: `1..65535`, порты должны отличаться
- `http.bind_host` и `ws.bind_host`: не пустые
- `http.max_payload_bytes`, `ws.max_payload_bytes`: > 0
- `queues.ingest_capacity`, `queues.history_capacity`, `queues.workers`: > 0
- `security.server_secret`: не пустой, длина >= 16
- `security.anti_replay.max_skew_ms`, `nonce_ttl_ms`, `nonce_capacity`: > 0
- `storage.path`: не пустой
- `storage.min_free_bytes`: >= 0
- `logging.level`: `trace|debug|info|warn|error`
- `peers[].id`: не пустой, уникальный
- `peers[].url`: не пустой, начинается с `http://` или `https://`

## Коды ошибок загрузчика/валидатора
- `file_not_found`: файл отсутствует или не открывается.
- `parse_error`: ошибка парсинга JSON.
- `missing`: обязательное поле отсутствует или пустое.
- `type_mismatch`: тип поля не соответствует ожидаемому.
- `out_of_range`: значение вне допустимых границ.
- `invalid_format`: неверный формат (например, env или node_id).
- `conflict`: конфликт значений (например, одинаковые порты или дубликаты peer id).

## Безопасность
`server_secret` никогда не логируется. В логах присутствуют только безопасные поля (node_id, env, порты и т.д.).
