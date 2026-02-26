/// \file fake_dfh_adapter.hpp
/// \brief In-memory реализация `IDfhAdapter` для тестов и отладки.
/// \details Хранит payload, метаданные и SHA-256 хеши блоков в памяти
/// с потокобезопасным доступом через общий `std::mutex`.
///
#pragma once

#include "dfh_adapter.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <vector>

namespace dfh_node {

/// \brief In-memory адаптер хранения, реализующий контракт `IDfhAdapter`.
/// \details Используется как тестовый backend без реальной БД.
class FakeDfhAdapter : public IDfhAdapter {
public:
    std::unique_ptr<IngestResponse> ingest_structured(std::unique_ptr<IngestRequest> req) override;
    std::unique_ptr<QueryHistoryResponse> query_history(std::unique_ptr<QueryHistoryRequest> req) override;
    std::unique_ptr<GetBlockDfhbinResponse>
    get_block_dfhbin(std::unique_ptr<GetBlockDfhbinRequest> req) override;
    std::unique_ptr<ListBlockMetaResponse>
    list_block_meta(std::unique_ptr<ListBlockMetaRequest> req) override;
    std::unique_ptr<GetBlockHashResponse> get_block_hash(std::unique_ptr<GetBlockHashRequest> req) override;

    /// \brief Полностью очищает внутреннее состояние адаптера.
    void reset();

private:
    std::map<BlockKey, std::vector<std::uint8_t>> m_storage;
    std::map<BlockKey, BlockMeta> m_meta;
    std::map<BlockKey, std::array<std::uint8_t, 32>> m_hashes;
    mutable std::mutex m_mutex;
};

} // namespace dfh_node
