/// \file fake_dfh_adapter.cpp
/// \brief Реализация in-memory адаптера хранения `FakeDfhAdapter`.
/// \details Обеспечивает контракт `IDfhAdapter` без реальной БД и
/// хранит данные в `std::map` с сортировкой по `BlockKey`.
///

#include "fake_dfh_adapter.hpp"

#include "security/sha256_utils.hpp"

#include <chrono>
#include <string>
#include <utility>

namespace dfh_node {

namespace {

std::int64_t now_ms() {
    const auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::array<std::uint8_t, 32> compute_payload_hash(const std::vector<std::uint8_t> &payload) {
    std::array<std::uint8_t, 32> hash{};
    const std::string data(reinterpret_cast<const char *>(payload.data()), payload.size());
    compute_sha256_raw(data, hash.data());
    return hash;
}

std::int64_t block_ts_for_timeframe(Timeframe tf, std::int64_t ts_ms) {
    if (tf == Timeframe::Ticks) {
        return block_ts_for_ticks(ts_ms);
    }

    return block_ts_for_m1bars(ts_ms);
}

bool matches_non_empty_filter(const std::string &filter, const std::string &value) {
    return filter.empty() || filter == value;
}

} // namespace

bool operator<(const BlockKey &a, const BlockKey &b) {
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

std::unique_ptr<IngestResponse> FakeDfhAdapter::ingest_structured(std::unique_ptr<IngestRequest> req) {
    auto resp = std::make_unique<IngestResponse>();
    if (!req) {
        resp->status = AdapterStatus::Error;
        resp->error_code = "invalid_argument";
        return resp;
    }

    const auto new_hash = compute_payload_hash(req->payload);

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_hashes.find(req->key);
    if (it != m_hashes.end() && it->second == new_hash) {
        resp->status = AdapterStatus::Ignore;
        return resp;
    }

    m_storage[req->key] = req->payload;
    m_hashes[req->key] = new_hash;
    m_meta[req->key] = BlockMeta{
        req->key, req->key.block_ts, req->key.block_ts, 0, now_ms(),
    };

    resp->status = AdapterStatus::Ok;
    return resp;
}

std::unique_ptr<QueryHistoryResponse> FakeDfhAdapter::query_history(std::unique_ptr<QueryHistoryRequest> req) {
    auto resp = std::make_unique<QueryHistoryResponse>();
    if (!req) {
        resp->status = AdapterStatus::Error;
        resp->error_code = "invalid_argument";
        return resp;
    }

    resp->status = AdapterStatus::Ok;
    if (req->to_ms <= req->from_ms) {
        return resp;
    }

    const std::int64_t start_block = block_ts_for_timeframe(req->tf, req->from_ms);
    const std::int64_t end_block = block_ts_for_timeframe(req->tf, req->to_ms - 1);

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &entry : m_storage) {
        const BlockKey &key = entry.first;
        if (!matches_non_empty_filter(req->provider, key.provider)) {
            continue;
        }
        if (!matches_non_empty_filter(req->symbol, key.symbol)) {
            continue;
        }
        if (!matches_non_empty_filter(req->source, key.source)) {
            continue;
        }
        if (key.tf != req->tf) {
            continue;
        }
        if (key.block_ts < start_block || key.block_ts > end_block) {
            continue;
        }

        resp->chunks.push_back(HistoryChunk{key, entry.second});
    }

    return resp;
}

std::unique_ptr<GetBlockDfhbinResponse> FakeDfhAdapter::get_block_dfhbin(std::unique_ptr<GetBlockDfhbinRequest> req) {
    auto resp = std::make_unique<GetBlockDfhbinResponse>();
    if (!req) {
        resp->status = AdapterStatus::Error;
        resp->error_code = "invalid_argument";
        return resp;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_storage.find(req->key);
    if (it == m_storage.end()) {
        resp->status = AdapterStatus::Error;
        resp->error_code = "not_found";
        return resp;
    }

    resp->status = AdapterStatus::Ok;
    resp->payload = it->second;
    return resp;
}

std::unique_ptr<ListBlockMetaResponse> FakeDfhAdapter::list_block_meta(std::unique_ptr<ListBlockMetaRequest> req) {
    auto resp = std::make_unique<ListBlockMetaResponse>();
    if (!req) {
        resp->status = AdapterStatus::Error;
        resp->error_code = "invalid_argument";
        return resp;
    }

    resp->status = AdapterStatus::Ok;
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto &entry : m_meta) {
        const BlockKey &key = entry.first;
        if (!matches_non_empty_filter(req->provider, key.provider)) {
            continue;
        }
        if (!matches_non_empty_filter(req->symbol, key.symbol)) {
            continue;
        }
        if (!matches_non_empty_filter(req->source, key.source)) {
            continue;
        }
        if (key.tf != req->tf) {
            continue;
        }
        if (req->from_block_ts.has_value() && key.block_ts < *req->from_block_ts) {
            continue;
        }
        if (req->to_block_ts.has_value() && key.block_ts > *req->to_block_ts) {
            continue;
        }

        resp->blocks.push_back(entry.second);
    }

    return resp;
}

std::unique_ptr<GetBlockHashResponse> FakeDfhAdapter::get_block_hash(std::unique_ptr<GetBlockHashRequest> req) {
    auto resp = std::make_unique<GetBlockHashResponse>();
    if (!req) {
        resp->status = AdapterStatus::Error;
        resp->error_code = "invalid_argument";
        return resp;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_hashes.find(req->key);
    if (it == m_hashes.end()) {
        resp->status = AdapterStatus::Error;
        resp->error_code = "not_found";
        return resp;
    }

    resp->status = AdapterStatus::Ok;
    resp->hash = it->second;
    return resp;
}

void FakeDfhAdapter::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_storage.clear();
    m_meta.clear();
    m_hashes.clear();
}

} // namespace dfh_node
