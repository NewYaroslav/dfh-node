# Зависимости третьих сторон

## Зачем `third_party/` и почему submodules
Мы храним сторонний код в `third_party/` и подключаем его как git submodules, чтобы сборки были воспроизводимыми. Каждая зависимость закрепляется на конкретный тег или коммит — это делает пересборки детерминированными и защищает от случайных апгрейдов.

## Правила
1) Только pinned коммиты/теги (никаких плавающих веток).
2) Обновления зависимостей — отдельные PR/коммиты с changelog/заметками.
3) У каждой зависимости должна быть лицензия; она фиксируется в таблице.
4) Минимизировать компоненты Boost при подключении; Boost допустим.
5) По возможности не патчить зависимости; если патчим — через patch-файл с объяснением.

## Таблица зависимостей (шаблон)
| Name | Upstream URL | Pinned ref (tag/commit) | License | Notes |
| --- | --- | --- | --- | --- |
| nlohmann/json | https://github.com/nlohmann/json | v3.11.3 | MIT | Header-only JSON library |
| log-it-cpp | https://github.com/NewYaroslav/log-it-cpp | v1.0.1 | MIT | Logging library |
| asio | https://github.com/chriskohlhoff/asio | 28d9b8d6df708024af5227c551673fdb2519f5bf | Boost Software License 1.0 | Standalone Asio headers for Simple-Web-Server (legacy API compatible) |
| simple-web-server | https://github.com/eidheim/Simple-Web-Server | 35ebb10782507f887802df64a2b6bfc8b427d81f | MIT | Header-only HTTP/HTTPS server |
| simple-websocket-server | https://gitlab.com/eidheim/Simple-WebSocket-Server | 89e5677789d096374edb93aaabaf23799a7e1692 | MIT | Header-only WS server |
| msgpack-c | https://github.com/msgpack/msgpack-c | 44c0f705c9a60217d7e07de844fb13ce4c1c1e6e | BSL-1.0 | Header-only MessagePack C++ |

## Процесс обновления
1) Обновить ref сабмодуля (закрепить новый тег/коммит).
2) Просмотреть changelog/релиз-заметки и зафиксировать ключевые изменения.
3) Проверить сборку локально.
4) Обновить таблицу и любые заметки в `third_party/README.md`.
5) Прогнать тесты.
6) Закоммитить с понятным сообщением (например, "Update <dep> to <ref>").
