# Configuration (draft)

## Purpose
Конфиг управляет сетевыми портами, лимитами, безопасностью, peers, а также режимами работы.
Документ фиксирует группы настроек и их назначение, без привязки к конкретному формату.
Формат конфигурации (yaml/json/toml и т.п.) будет выбран позже.

## Suggested sections (outline)
1. Node identity
   - node_id (если нужен)
   - environment (dev/prod)
2. Network
   - http listen address/port
   - ws listen address/port
   - behind reverse proxy (TLS termination external)
3. Queues & workers
   - ingest queue size
   - history queue size
   - worker threads counts / scheduling policy
4. Limits
   - default req/sec
   - default max ws connections
   - per-scope overrides (optional)
5. Storage/DFH adapter
   - connection/settings placeholder (пока мок)
   - merge vs replace mode (default merge)
6. Peers & sync
   - list of peers (static)
   - sync interval / triggers (high-level)
7. Security
   - server_secret for HMAC/fingerprint
   - anti-replay window (seconds)
   - nonce cache TTL/size
8. Disk protection
   - minimum free space threshold (e.g., 2 GB)
   - behavior when below threshold (disable writes)
9. Logging
   - log level
   - log sinks (console/file) — high-level

## Open questions
- Какой формат конфигурации выбрать и почему?
- Какие дефолты по лимитам и очередям считаются безопасными?
- Нужен ли node_id и как он должен генерироваться?
- Какие критерии для merge vs replace должны быть по умолчанию?
- Какие метрики обязательны для health-check?
