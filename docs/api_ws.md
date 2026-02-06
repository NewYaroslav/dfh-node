# WebSocket API (draft)

## Purpose
WebSocket используется для ingest/history/subscribe, где control идет в JSON или MessagePack,
а dfhbin передается бинарными фреймами.
Документ описывает типы сообщений и базовые правила обмена.

## Connection endpoints
- /ws/json — control в JSON
- /ws/msgpack — control в MessagePack
Семантика одинакова.

## Message types (outline)
1. Control message (text/binary structured)
   - поле op: ingest | history | subscribe
   - поле request_id: корреляция запрос/ответ
   - payload: параметры запроса (provider/symbol/type/tf/from/to или ts для dfhbin)
2. Data frames
   - dfhbin как binary frames (payload = raw dfhbin block)
   - structured data (ticks/bars) как JSON/MsgPack (если не dfhbin)
3. Responses
   - ack/ok
   - ignore
   - error (code + message)
   - server overload: “dropped” для ingest

## Flow examples (словами, без кода)
- Ingest flow:
  - client sends control(op=ingest)
  - client streams data messages (JSON/MsgPack) или dfhbin binary frames
  - server replies per request_id (ok/error)
- History flow:
  - client sends control(op=history)
  - server streams results (dfhbin binary frames или structured messages)
  - server signals completion (например финальный control “done” — как идея)

## Limits & prioritization
- scheduler uses high-priority over low-priority lanes
- обычно ingest идет в high-priority lane, history в low-priority lane
- low-priority requests can be rejected under load
- ws connection limits per token

## Open questions
- Нужна ли отдельная команда для ping/pong на уровне control?
- Как фиксировать окончание history потока и его статус?
- Какие коды ошибок и уровни детализации должны быть обязательны?
- Какие лимиты по размеру фреймов следует принять по умолчанию?
