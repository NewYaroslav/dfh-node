/// \file dfh_adapter.hpp
/// \brief Интерфейс адаптера слоя хранения DFH.
/// \details Фиксирует синхронный контракт между worker-задачами и хранилищем
/// через DTO запросов/ответов на `std::unique_ptr`.
///
#pragma once

#include "dfh_adapter_dto.hpp"

#include <memory>

namespace dfh_node {

/// \brief Абстрактный интерфейс адаптера хранения DFH.
/// \details Все методы синхронные и принимают/возвращают владение DTO через
/// `std::unique_ptr`, чтобы исключить лишние копии payload-данных.
class IDfhAdapter {
public:
    virtual ~IDfhAdapter() = default;

    /// \brief Сохраняет структурированный блок данных.
    /// \param req Запрос на ingest с ключом и payload.
    /// \return Ответ со статусом операции ingest.
    virtual std::unique_ptr<IngestResponse> ingest_structured(std::unique_ptr<IngestRequest> req)
        = 0;

    /// \brief Возвращает набор блоков истории за диапазон времени.
    /// \param req Запрос истории с фильтрами и временными границами.
    /// \return Ответ со статусом и `chunks` истории.
    virtual std::unique_ptr<QueryHistoryResponse>
    query_history(std::unique_ptr<QueryHistoryRequest> req)
        = 0;

    /// \brief Возвращает бинарный блок `dfhbin` по ключу.
    /// \param req Запрос конкретного блока по `BlockKey`.
    /// \return Ответ со статусом и payload блока.
    virtual std::unique_ptr<GetBlockDfhbinResponse>
    get_block_dfhbin(std::unique_ptr<GetBlockDfhbinRequest> req)
        = 0;

    /// \brief Возвращает список метаданных блоков по фильтрам.
    /// \param req Запрос списка метаданных и границ `block_ts`.
    /// \return Ответ со статусом и списком `BlockMeta`.
    virtual std::unique_ptr<ListBlockMetaResponse>
    list_block_meta(std::unique_ptr<ListBlockMetaRequest> req)
        = 0;

    /// \brief Возвращает SHA-256 хеш блока по ключу.
    /// \param req Запрос хеша конкретного блока.
    /// \return Ответ со статусом и массивом байт хеша.
    virtual std::unique_ptr<GetBlockHashResponse>
    get_block_hash(std::unique_ptr<GetBlockHashRequest> req)
        = 0;
};

} // namespace dfh_node
