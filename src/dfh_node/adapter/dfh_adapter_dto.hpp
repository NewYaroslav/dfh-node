/// \file dfh_adapter_dto.hpp
/// \brief DTO-контракты адаптера хранения DFH и вспомогательные типы.
/// \details Описывает ключи блоков, запросы/ответы адаптера и инварианты
/// статусов для будущей интеграции с очередями и transport-слоем.
///
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dfh_node {

/// \brief Таймфрейм блока данных в хранилище.
enum class Timeframe : std::uint32_t { Ticks = 0, M1 = 60 };

/// \brief Ключ блока в хранилище.
/// \details Идентичность задаётся строковыми полями и `block_ts`;
/// числовые идентификаторы (`provider_id`/`symbol_id`) используются только
/// как подсказки в запросах.
struct BlockKey {
    std::string provider;
    std::string symbol;
    std::string source;
    Timeframe tf = Timeframe::Ticks;
    std::int64_t block_ts = 0;
};

/// \brief Задаёт строгий порядок ключей блока для ассоциативных контейнеров.
/// \param a Левый операнд сравнения.
/// \param b Правый операнд сравнения.
/// \return `true`, если `a` лексикографически меньше `b`.
inline bool operator<(const BlockKey &a, const BlockKey &b) {
    if (a.provider != b.provider) {
        return a.provider < b.provider;
    }
    if (a.symbol != b.symbol) {
        return a.symbol < b.symbol;
    }
    if (a.source != b.source) {
        return a.source < b.source;
    }
    if (a.tf != b.tf) {
        return a.tf < b.tf;
    }
    return a.block_ts < b.block_ts;
}

/// \brief Возвращает начало часового блока для тиков.
/// \param ts_ms Метка времени в миллисекундах UTC.
/// \return Начало часа в миллисекундах UTC (floor-деление для отрицательных значений).
std::int64_t block_ts_for_ticks(std::int64_t ts_ms);

/// \brief Возвращает начало дневного блока для M1 баров.
/// \param ts_ms Метка времени в миллисекундах UTC.
/// \return Начало дня в миллисекундах UTC (floor-деление для отрицательных значений).
std::int64_t block_ts_for_m1bars(std::int64_t ts_ms);

/// \brief Статус ответа адаптера.
enum class AdapterStatus : std::uint8_t {
    Ok,     ///< Операция успешно выполнена.
    Ignore, ///< Данные эквивалентны уже сохранённым (без изменения состояния).
    Error   ///< Операция завершилась ошибкой.
};

/// \brief Метаданные блока в хранилище.
struct BlockMeta {
    BlockKey key;
    std::int64_t first_ts = 0;
    std::int64_t last_ts = 0;
    std::size_t record_count = 0;
    std::int64_t updated_at = 0;
    std::array<std::uint8_t, 32> hash{}; ///< SHA-256 блока; заполняется адаптером.
};

/// \brief Один блок результата истории.
/// \details `chunks` в `QueryHistoryResponse` должны быть отсортированы по
/// `key.block_ts` по возрастанию.
struct HistoryChunk {
    BlockKey key;
    std::vector<std::uint8_t> payload;
};

/// \brief Запрос на ingest структурированного блока.
struct IngestRequest {
    BlockKey key;
    std::vector<std::uint8_t> payload;
};

/// \brief Запрос истории за временной диапазон.
/// \details `from_ms` включительно, `to_ms` исключительно.
/// Границы блоков вычисляются как:
/// `start_block = block_ts_for_*(from_ms)`,
/// `end_block = block_ts_for_*(to_ms - 1)`.
struct QueryHistoryRequest {
    std::string provider;
    std::string symbol;
    std::string source;
    Timeframe tf = Timeframe::Ticks;
    std::int64_t from_ms = 0;
    std::int64_t to_ms = 0;
    std::optional<std::uint32_t> provider_id;
    std::optional<std::uint32_t> symbol_id;
};

/// \brief Запрос бинарного блока `dfhbin` по ключу.
struct GetBlockDfhbinRequest {
    BlockKey key;
};

/// \brief Запрос списка метаданных блоков с фильтрами.
/// \details `from_block_ts` и `to_block_ts` — включительные границы.
/// `std::nullopt` означает отсутствие ограничения по соответствующей границе.
struct ListBlockMetaRequest {
    std::string provider;
    std::string symbol;
    std::string source;
    Timeframe tf = Timeframe::Ticks;
    std::optional<std::int64_t> from_block_ts;
    std::optional<std::int64_t> to_block_ts;
    std::optional<std::uint32_t> provider_id;
    std::optional<std::uint32_t> symbol_id;
};

/// \brief Запрос хеша блока по ключу.
struct GetBlockHashRequest {
    BlockKey key;
};

/// \brief Ответ на ingest.
/// \details При `AdapterStatus::Error` `error_code` обязан быть непустым.
/// Разрешённый минимальный набор: `not_found`, `invalid_argument`, `internal`.
struct IngestResponse {
    AdapterStatus status = AdapterStatus::Error;
    std::string error_code;
};

/// \brief Запрос merge raw `dfhbin`-блока при межнодовой синхронизации.
struct MergeBlockDfhbinRequest {
    BlockKey key;
    std::vector<std::uint8_t> bytes; ///< Raw `dfhbin` payload.
};

/// \brief Ответ на merge `dfhbin`-блока.
/// \details `Ok` = блок сохранён; `Ignore` = локальные данные уже актуальны;
/// `Error` = произошла ошибка.
struct MergeBlockDfhbinResponse {
    AdapterStatus status = AdapterStatus::Error;
    std::string error_code;
};

/// \brief Ответ на запрос истории.
/// \details При `AdapterStatus::Error` `error_code` обязан быть непустым,
/// а `chunks` обязан быть пустым.
struct QueryHistoryResponse {
    AdapterStatus status = AdapterStatus::Error;
    std::string error_code;
    std::vector<HistoryChunk> chunks;
};

/// \brief Ответ на запрос блока `dfhbin`.
/// \details При `AdapterStatus::Error` `error_code` обязан быть непустым,
/// а `payload` обязан быть пустым.
struct GetBlockDfhbinResponse {
    AdapterStatus status = AdapterStatus::Error;
    std::string error_code;
    std::vector<std::uint8_t> payload;
};

/// \brief Ответ на запрос метаданных блоков.
/// \details При `AdapterStatus::Error` `error_code` обязан быть непустым,
/// а `blocks` обязан быть пустым.
struct ListBlockMetaResponse {
    AdapterStatus status = AdapterStatus::Error;
    std::string error_code;
    std::vector<BlockMeta> blocks;
};

/// \brief Ответ на запрос хеша блока.
/// \details При `AdapterStatus::Error` `error_code` обязан быть непустым,
/// а `hash` обязан быть равен `{}`.
struct GetBlockHashResponse {
    AdapterStatus status = AdapterStatus::Error;
    std::string error_code;
    std::array<std::uint8_t, 32> hash{};
};

} // namespace dfh_node
