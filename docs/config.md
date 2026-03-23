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
    "max_payload_bytes": 10000000,
    "request_timeout_ms": 30000,
    "history_max_range_ms": 86400000,
    "history_max_bytes": 104857600
  },
  "ws": {
    "bind_host": "0.0.0.0",
    "port": 8081,
    "max_payload_bytes": 10000000,
    "request_timeout_ms": 30000
  },
  "queues": {
    "high_capacity": 10000,
    "low_capacity": 5000,
    "workers": 4
  },
  "security": {
    "server_secret": "test-secret-key-16chars",
    "anti_replay": {
      "enabled": true,
      "max_skew_ms": 5000,
      "nonce_ttl_ms": 60000,
      "nonce_capacity": 10000,
      "require_for_scopes": ["write", "admin", "sync"]
    }
  },
  "sync": {
    "enabled": false,
    "pull_interval_ms": 60000,
    "request_timeout_ms": 30000,
    "meta_max_blocks": 10000,
    "max_blocks_per_cycle": 1000,
    "max_parallel_downloads": 4,
    "outbound_token": ""
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
    "max_payload_bytes": 20000000,
    "request_timeout_ms": 30000,
    "history_max_range_ms": 172800000,
    "history_max_bytes": 209715200
  },
  "ws": {
    "bind_host": "0.0.0.0",
    "port": 8081,
    "max_payload_bytes": 20000000,
    "request_timeout_ms": 30000
  },
  "queues": {
    "high_capacity": 20000,
    "low_capacity": 10000,
    "workers": 8
  },
  "security": {
    "server_secret": "prod-secret-key-32chars",
    "anti_replay": {
      "enabled": true,
      "max_skew_ms": 5000,
      "nonce_ttl_ms": 60000,
      "nonce_capacity": 20000,
      "require_for_scopes": ["write", "admin", "sync"]
    }
  },
  "sync": {
    "enabled": true,
    "pull_interval_ms": 60000,
    "request_timeout_ms": 30000,
    "meta_max_blocks": 10000,
    "max_blocks_per_cycle": 1000,
    "max_parallel_downloads": 4,
    "outbound_token": "sync-outbound-token"
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
- `sync` (object, optional).
- `peers` (array, optional, default: пустой).
- `storage` (object, optional).
- `logging` (object, optional).

### http
- `bind_host` (string, default: `0.0.0.0`).
- `port` (int, default: `8080`).
- `max_payload_bytes` (int, default: `10000000`).
- `request_timeout_ms` (int, default: `30000`): таймаут отложенного ответа в миллисекундах (`0` = отключён).
- `history_max_range_ms` (int, default: `86400000`): максимальный диапазон запроса history (`to_ms - from_ms`) в миллисекундах.
- `history_max_bytes` (int, default: `104857600`): максимальный размер ответа history в байтах.

### ws
- `bind_host` (string, default: `0.0.0.0`).
- `port` (int, default: `8081`).
- `max_payload_bytes` (int, default: `10000000`).
- `request_timeout_ms` (int, default: `30000`): таймаут WS-задач в миллисекундах (`0` = отключён).

### queues
- `high_capacity` (int, default: `10000`).
- `low_capacity` (int, default: `5000`).
- `workers` (int, default: `4`).

### security
- `server_secret` (string, required).
- `anti_replay` (object, optional).

#### security.anti_replay
- `enabled` (bool, default: `true`).
- `max_skew_ms` (int, default: `5000`).
- `nonce_ttl_ms` (int, default: `60000`).
- `nonce_capacity` (int, default: `10000`).
- `require_for_scopes` (array<string>, default: `["write","admin","sync"]`).

### sync
- `enabled` (bool, default: `false`): включает фоновый pull loop.
- `pull_interval_ms` (int, default: `60000`): интервал между sync-циклами.
- `request_timeout_ms` (int, default: `30000`): таймаут исходящих HTTP sync-запросов.
- `meta_max_blocks` (int, default: `10000`): верхняя граница числа блоков, принимаемых из `/sync/meta` за один peer.
- `max_blocks_per_cycle` (int, default: `1000`): верхняя граница числа скачиваемых блоков за цикл.
- `max_parallel_downloads` (int, default: `4`): зарезервировано под ограничение параллельных загрузок; текущая реализация `PeerSyncService` выполняет загрузки последовательно.
- `outbound_token` (string, default: пусто): plaintext токен для исходящих запросов к peers.

### peers
Массив объектов:
- `id` (string)
- `url` (string)

### storage
- `path` (string, default: `./data`).
- `min_free_bytes` (int, default: `2000000000`).
- В runtime каталог `storage.path` используется и для `DiskMonitor`, и для файла динамических ключей `storage.path/keys.mdbx`.

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
- `http.request_timeout_ms`: >= 0
- `ws.request_timeout_ms`: >= 0
- `http.history_max_range_ms`, `http.history_max_bytes`: > 0
- `queues.high_capacity`, `queues.low_capacity`, `queues.workers`: > 0
- `security.server_secret`: не пустой, длина >= 16
- `security.anti_replay.max_skew_ms`, `nonce_ttl_ms`, `nonce_capacity`: > 0
- `security.anti_replay.require_for_scopes`: при `enabled=true` не должен быть пустым
- `sync.pull_interval_ms`, `sync.request_timeout_ms`, `sync.meta_max_blocks`, `sync.max_blocks_per_cycle`,
  `sync.max_parallel_downloads`: при `sync.enabled=true` должны быть > 0
- `sync.outbound_token`: обязателен, если `sync.enabled=true` и `peers` не пустой
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

## Примечание по capacity rule
Рекомендация из протокола: `nonce_capacity >= peak_rps_per_fingerprint * nonce_ttl_seconds * 1.5`.

Текущая реализация валидатора использует `auth.rps_limit` как baseline-оценку `peak_rps_per_fingerprint`
и пишет `WARN` в `std::clog`, если:

`nonce_capacity < auth.rps_limit * nonce_ttl_seconds * 1.5`

Это предупреждение, не ошибка валидации.
