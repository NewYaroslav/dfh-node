# Configuration (draft)

## Purpose
Конфиг управляет сетевыми портами, лимитами, безопасностью, peers, а также режимами работы.
Этот документ описывает, какие группы настроек будут доступны и для чего они нужны.

## Content outline
1. General
   - режимы запуска и окружение
   - пути к данным и кэшу
2. Network
   - HTTP порт и адрес
   - WebSocket порт и адрес
   - таймауты и keep-alive
3. Limits
   - rate limits (HTTP и WS)
   - лимиты очередей (ingest/history)
   - лимиты на диапазон истории и размер ответа
4. Security
   - источник ключей и rotation
   - anti-replay настройки (timestamp/nonce окна)
   - доверенные прокси и заголовки
5. Sync
   - список peers
   - режимы pull и интервалы
   - лимиты синхронизации
6. Logging & metrics
   - уровни логирования
   - метрики и health-check
