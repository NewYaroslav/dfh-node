# Модель безопасности

## Назначение
Документ описывает модель безопасности: API-ключи, scope-права, rate limiting, anti-replay и правила хранения секретов.
Цель — зафиксировать базовые принципы и согласовать минимальный обязательный набор мер.

Статус на текущий момент:
- Core-часть безопасности (scope, rate limiting, anti-replay, nonce store, canonical request, подписи) реализована в библиотеке `dfh_node`.
- HTTP/WS транспорт ещё не подключён в runtime (`dfh_node_app` работает в one-shot режиме), поэтому transport-часть ниже описывает контракт интеграции.

## Структура
1. Модель угроз (кратко)
   - интернет-трафик, reverse proxy для TLS
   - злоупотребление API и replay-атаки
2. API-ключи и scope-права
   - scope-права: read/write/admin/sync
   - admin-ключи в конфиге, пользовательские ключи в хранилище (планируется)
3. Правила хранения токенов
   - избегать хранения plaintext token
   - fingerprint = HMAC-SHA256(server_secret, token)
   - хеширование хранимых токенов при необходимости (PBKDF2/Argon2)
4. Кэширование
   - TTL auth-кэша 30–60s
   - инвалидация по updated_at/revoked_at
5. Ограничение скорости
   - req/sec
   - лимит ws-соединений
6. Anti-replay (обязательно)
   - timestamp + nonce + HMAC-подпись
   - сервер проверяет временное окно и уникальность nonce (TTL-хранилище)
   - идея канонической строки: method/path/query/ts/nonce/body_hash
7. Логирование и аудит
   - логировать попытки неавторизованного доступа
   - логировать операции управления admin-ключами
8. Эксплуатационные заметки
   - TLS терминируется внешним прокси
   - управление секретами (не коммитить секреты; использовать env/secret store)

## Открытые вопросы
- Где хранить пользовательские ключи и как версионировать их ревокацию? (актуально)
- Какие значения временных окон и TTL считать безопасными для конкретного профиля нагрузки? (частично закрыто: есть max_skew, nonce_ttl и правило capacity)
- Нужна ли обязательная ротация ключей и как ее автоматизировать? (актуально)
- Какие события должны попадать в аудит по умолчанию? (актуально)

## Закрытые вопросы
- Какая точная схема подписи и порядок полей в канонической строке? (закрыто: зафиксированы signing_key, canonical-форматы HTTP/WS, порядок проверок и канонизация query)

## Протокол anti-replay

### Вывод ключа подписи

**Ключ подписи:** `signing_key = SHA256(token)` (32 сырых байта, НЕ plaintext token, НЕ server_secret)

**Почему SHA256(token):**
- Клиент может вычислить signing_key локально (не требует server_secret)
- Сервер вычисляет тот же ключ из token в заголовке Authorization (HTTP) или из handshake (WS)
- Для WS: signing_key хранится в памяти WS-сессии (32 байта), plaintext token не хранится

**HTTP-поток:**
1. Клиент: signing_key = SHA256(token), signature = HMAC(signing_key, canonical_string)
2. Сервер: извлекает token из Authorization → вычисляет signing_key = SHA256(token) на лету
3. Сервер: проверяет HMAC(signing_key, canonical_string) == X-DFH-Signature (constant-time)

**WS-поток:**
1. Handshake/upgrade: извлечь token → вычислить fingerprint → найти AuthContext → вычислить signing_key = SHA256(token) → сохранить в WsConnectionContext (32 байта)
2. WS message: извлечь signing_key из connection context → проверить HMAC(signing_key, canonical_string) == signature

\* Примечание: это целевой контракт для transport-слоя. В текущем runtime WS handshake/message pipeline ещё не активирован.

### Порядок валидации

**Критично:** порядок проверок защищает от DoS на NonceStore:
1. **Parse:** проверить формат (timestamp число, nonce 16 hex, signature 64 hex, signing_key 32 bytes)
2. **Skew:** `abs(now_ms - request_ts) <= max_skew_ms` (разрешает будущее в пределах окна)
3. **Signature:** проверка HMAC (constant-time через CRYPTO_memcmp)
4. **Nonce:** только после валидной подписи → check_and_record (защита от spam с invalid sig)

### TTL и часы

**NonceStore TTL:** считается от server_now, НЕ от request_ts
- `insertion_time = server_now` при вставке
- `cleanup: now - insertion_time > ttl_ms`
- Защита от клиентских часов (не зависит от request_ts)

### Формат канонической строки

**HTTP:**
```
METHOD\n
PATH\n
QUERY_STRING\n
TIMESTAMP\n
NONCE\n
BODY_HASH
```
(Последний `\n` НЕ включается, строка заканчивается на BODY_HASH)

**WS:**
```
ENDPOINT\n
OP\n
MSG_ID\n
TIMESTAMP\n
NONCE\n
PAYLOAD_HASH
```
(Последний `\n` НЕ включается, строка заканчивается на PAYLOAD_HASH)

**Хеш пустого тела:** для GET/HEAD → `sha256("") = e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`

**WS включает op/msg_id/payload_hash:** защита от подмены смысла сообщения (критично для dfhbin)

### Канонизация query-параметров

**Процесс:**
1. Декодировать URL-encoding (percent-decode: `+` → пробел, `%2F` → `/`, `%20` → пробел)
2. Отсортировать по (key, value) в лексикографическом порядке (ASCII-порядок после decode)
3. Канонически перекодировать (RFC3986: пробел → `%20`, `/` → `%2F`, НЕ использовать `+`)

**Пример:**
- Вход: `?symbol=BTC%2FUSD&exchange=binance&order=+asc`
- Декодирование: `symbol=BTC/USD`, `exchange=binance`, `order= asc` (пробел)
- Сортировка: `exchange=binance`, `order= asc`, `symbol=BTC/USD`
- Перекодирование: `exchange=binance&order=%20asc&symbol=BTC%2FUSD`

### Правило ёмкости

**Формула:** `nonce_capacity >= peak_rps_per_fingerprint * nonce_ttl_seconds * 1.5`

**Почему:** LRU-eviction в пределах TTL может привести к replay (если capacity слишком мал)

**Пример:** rps=100, ttl=60s → capacity >= 100 * 60 * 1.5 = 9000

**Валидация:** мягкая (WARNING в логе), НЕ жёсткая ошибка конфига.

\* Примечание по текущей реализации: в `config_validator` warning считается по baseline `peak_rps_per_fingerprint = 1`
и выводится в `std::clog`.
