# Security model (draft)

## Purpose
Документ описывает модель безопасности: API keys, scopes, rate limits, anti-replay и правила хранения секретов.
Цель — определить базовые принципы и договориться о минимальном наборе мер.

## Outline
1. Threat model (коротко)
   - internet traffic, reverse proxy for TLS
   - API abuse and replay attacks
2. API keys & scopes
   - scopes: read/write/admin/sync
   - admin keys in config, user keys in storage (planned)
3. Token storage rules
   - avoid plaintext token storage
   - fingerprint = HMAC-SHA256(server_secret, token)
   - password hashing for stored tokens if applicable (PBKDF2/Argon2 mention)
4. Caching
   - auth cache TTL 30–60s
   - invalidation via updated_at/revoked_at
5. Rate limiting
   - req/sec
   - ws connections limit
6. Anti-replay (must-have)
   - timestamp + nonce + HMAC signature
   - server verifies time window and nonce uniqueness (TTL store)
   - canonical string idea: method/path/query/ts/nonce/body_hash
7. Logging & audit
   - log unauthorized access attempts
   - log admin key management operations
8. Operational notes
   - TLS termination handled externally
   - secret management (don’t commit secrets; env/secret store)

## Open questions
- Где хранить пользовательские ключи и как версионировать их ревокацию?
- Какая точная схема подписи и порядок полей в canonical string?
- Какие значения тайм-окон и TTL считаются безопасными?
- Нужна ли обязательная ротация ключей и как ее автоматизировать?
- Какие события должны попадать в аудит по умолчанию?
