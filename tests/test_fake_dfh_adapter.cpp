/// \file test_fake_dfh_adapter.cpp
/// \brief Юнит-тесты in-memory адаптера `FakeDfhAdapter`.
/// \details Проверяет контракт ingest/history/hash/meta и инварианты ответов при ошибках.
///
#include "adapter.hpp"
#include "security.hpp"
#include "test_helpers.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace dfh_node;

namespace {

BlockKey make_key(std::int64_t block_ts, Timeframe tf = Timeframe::Ticks, std::string provider = "p1",
                  std::string symbol = "s1", std::string source = "src") {
    BlockKey key;
    key.provider = std::move(provider);
    key.symbol = std::move(symbol);
    key.source = std::move(source);
    key.tf = tf;
    key.block_ts = block_ts;
    return key;
}

std::vector<std::uint8_t> bytes(std::initializer_list<std::uint8_t> data) { return std::vector<std::uint8_t>(data); }

void test_ingest_happy_path() {
    FakeDfhAdapter adapter;

    auto req1 = std::make_unique<IngestRequest>();
    req1->key = make_key(0);
    req1->payload = bytes({1, 2, 3});

    auto resp1 = adapter.ingest_structured(std::move(req1));
    CHECK_EQ(resp1->status, AdapterStatus::Ok);
    CHECK(resp1->error_code.empty());

    auto req2 = std::make_unique<IngestRequest>();
    req2->key = make_key(0);
    req2->payload = bytes({1, 2, 3});

    auto resp2 = adapter.ingest_structured(std::move(req2));
    CHECK_EQ(resp2->status, AdapterStatus::Ignore);
    CHECK(resp2->error_code.empty());
}

void test_ingest_overwrite() {
    FakeDfhAdapter adapter;

    auto req1 = std::make_unique<IngestRequest>();
    req1->key = make_key(1000);
    req1->payload = bytes({1, 2, 3});
    CHECK_EQ(adapter.ingest_structured(std::move(req1))->status, AdapterStatus::Ok);

    auto req2 = std::make_unique<IngestRequest>();
    req2->key = make_key(1000);
    req2->payload = bytes({9, 8, 7, 6});
    CHECK_EQ(adapter.ingest_structured(std::move(req2))->status, AdapterStatus::Ok);

    auto get_req = std::make_unique<GetBlockDfhbinRequest>();
    get_req->key = make_key(1000);
    auto get_resp = adapter.get_block_dfhbin(std::move(get_req));

    CHECK_EQ(get_resp->status, AdapterStatus::Ok);
    CHECK(get_resp->error_code.empty());
    CHECK_EQ(get_resp->payload, bytes({9, 8, 7, 6}));
}

void test_ingest_invalid_argument() {
    FakeDfhAdapter adapter;
    auto resp = adapter.ingest_structured(std::unique_ptr<IngestRequest>());

    CHECK_EQ(resp->status, AdapterStatus::Error);
    CHECK_EQ(resp->error_code, std::string("invalid_argument"));
}

void test_query_history_happy_path() {
    constexpr std::int64_t hour_ms = 3600LL * 1000;
    FakeDfhAdapter adapter;

    auto ingest = [&](std::int64_t block_ts, std::vector<std::uint8_t> payload) {
        auto req = std::make_unique<IngestRequest>();
        req->key = make_key(block_ts);
        req->payload = std::move(payload);
        CHECK_EQ(adapter.ingest_structured(std::move(req))->status, AdapterStatus::Ok);
    };

    ingest(0 * hour_ms, bytes({1}));
    ingest(1 * hour_ms, bytes({2}));
    ingest(3 * hour_ms, bytes({3}));

    auto req = std::make_unique<QueryHistoryRequest>();
    req->provider = "p1";
    req->symbol = "s1";
    req->source = "src";
    req->tf = Timeframe::Ticks;
    req->from_ms = 0;
    req->to_ms = 2 * hour_ms;

    auto resp = adapter.query_history(std::move(req));
    CHECK_EQ(resp->status, AdapterStatus::Ok);
    CHECK(resp->error_code.empty());
    CHECK_EQ(resp->chunks.size(), static_cast<std::size_t>(2));
    CHECK_EQ(resp->chunks[0].key.block_ts, 0 * hour_ms);
    CHECK_EQ(resp->chunks[1].key.block_ts, 1 * hour_ms);
}

void test_query_history_block_boundaries() {
    constexpr std::int64_t hour_ms = 3600LL * 1000;
    FakeDfhAdapter adapter;

    auto first = std::make_unique<IngestRequest>();
    first->key = make_key(10 * hour_ms);
    first->payload = bytes({10});
    CHECK_EQ(adapter.ingest_structured(std::move(first))->status, AdapterStatus::Ok);

    auto second = std::make_unique<IngestRequest>();
    second->key = make_key(11 * hour_ms);
    second->payload = bytes({11});
    CHECK_EQ(adapter.ingest_structured(std::move(second))->status, AdapterStatus::Ok);

    auto req = std::make_unique<QueryHistoryRequest>();
    req->provider = "p1";
    req->symbol = "s1";
    req->source = "src";
    req->tf = Timeframe::Ticks;
    req->from_ms = 10 * hour_ms + 15 * 60 * 1000;
    req->to_ms = 11 * hour_ms;

    auto resp = adapter.query_history(std::move(req));
    CHECK_EQ(resp->status, AdapterStatus::Ok);
    CHECK_EQ(resp->chunks.size(), static_cast<std::size_t>(1));
    CHECK_EQ(resp->chunks[0].key.block_ts, 10 * hour_ms);
}

void test_query_history_empty_range() {
    FakeDfhAdapter adapter;

    auto req = std::make_unique<QueryHistoryRequest>();
    req->provider = "p1";
    req->symbol = "s1";
    req->source = "src";
    req->tf = Timeframe::Ticks;
    req->from_ms = 10;
    req->to_ms = 10;

    auto resp = adapter.query_history(std::move(req));
    CHECK_EQ(resp->status, AdapterStatus::Ok);
    CHECK(resp->error_code.empty());
    CHECK(resp->chunks.empty());
}

void test_query_history_filters_and_m1_timeframe() {
    constexpr std::int64_t day_ms = 86400LL * 1000;
    FakeDfhAdapter adapter;

    auto ingest = [&](BlockKey key) {
        auto req = std::make_unique<IngestRequest>();
        req->key = std::move(key);
        req->payload = bytes({7, 7, 7});
        CHECK_EQ(adapter.ingest_structured(std::move(req))->status, AdapterStatus::Ok);
    };

    ingest(make_key(2 * day_ms, Timeframe::M1, "p1", "s1", "src"));
    ingest(make_key(2 * day_ms, Timeframe::M1, "p1", "other_symbol", "src"));
    ingest(make_key(2 * day_ms, Timeframe::M1, "p1", "s1", "other_source"));
    ingest(make_key(2 * day_ms, Timeframe::Ticks, "p1", "s1", "src"));
    ingest(make_key(2 * day_ms, Timeframe::M1, "other_provider", "s1", "src"));

    auto req = std::make_unique<QueryHistoryRequest>();
    req->provider = "p1";
    req->symbol = "s1";
    req->source = "src";
    req->tf = Timeframe::M1;
    req->from_ms = 2 * day_ms + 1;
    req->to_ms = 3 * day_ms;

    auto resp = adapter.query_history(std::move(req));
    CHECK_EQ(resp->status, AdapterStatus::Ok);
    CHECK(resp->error_code.empty());
    CHECK_EQ(resp->chunks.size(), static_cast<std::size_t>(1));
    CHECK_EQ(resp->chunks[0].key.block_ts, 2 * day_ms);
}

void test_get_block_dfhbin_happy_path() {
    FakeDfhAdapter adapter;

    auto ingest_req = std::make_unique<IngestRequest>();
    ingest_req->key = make_key(555);
    ingest_req->payload = bytes({4, 5, 6});
    CHECK_EQ(adapter.ingest_structured(std::move(ingest_req))->status, AdapterStatus::Ok);

    auto get_req = std::make_unique<GetBlockDfhbinRequest>();
    get_req->key = make_key(555);
    auto get_resp = adapter.get_block_dfhbin(std::move(get_req));

    CHECK_EQ(get_resp->status, AdapterStatus::Ok);
    CHECK(get_resp->error_code.empty());
    CHECK_EQ(get_resp->payload, bytes({4, 5, 6}));
}

void test_get_block_dfhbin_not_found() {
    FakeDfhAdapter adapter;

    auto req = std::make_unique<GetBlockDfhbinRequest>();
    req->key = make_key(999);
    auto resp = adapter.get_block_dfhbin(std::move(req));

    CHECK_EQ(resp->status, AdapterStatus::Error);
    CHECK_EQ(resp->error_code, std::string("not_found"));
    CHECK(resp->payload.empty());
}

void test_list_block_meta_filter() {
    FakeDfhAdapter adapter;
    const std::array<std::uint8_t, 32> zero_hash{};

    auto ingest = [&](std::int64_t ts, const std::string &provider, const std::string &symbol) {
        auto req = std::make_unique<IngestRequest>();
        req->key = make_key(ts, Timeframe::Ticks, provider, symbol, "src");
        req->payload = bytes({1, 2});
        CHECK_EQ(adapter.ingest_structured(std::move(req))->status, AdapterStatus::Ok);
    };

    ingest(1000, "pA", "s1");
    ingest(2000, "pA", "s2");
    ingest(3000, "pB", "s1");

    auto req = std::make_unique<ListBlockMetaRequest>();
    req->provider = "pA";
    req->symbol = "";
    req->source = "";
    req->tf = Timeframe::Ticks;

    auto resp = adapter.list_block_meta(std::move(req));
    CHECK_EQ(resp->status, AdapterStatus::Ok);
    CHECK_EQ(resp->blocks.size(), static_cast<std::size_t>(2));
    CHECK_EQ(resp->blocks[0].key.provider, std::string("pA"));
    CHECK_EQ(resp->blocks[1].key.provider, std::string("pA"));
    CHECK_NE(resp->blocks[0].hash, zero_hash);
    CHECK_NE(resp->blocks[1].hash, zero_hash);
}

void test_list_block_meta_bounds() {
    FakeDfhAdapter adapter;

    auto ingest = [&](std::int64_t ts) {
        auto req = std::make_unique<IngestRequest>();
        req->key = make_key(ts);
        req->payload = bytes({9});
        CHECK_EQ(adapter.ingest_structured(std::move(req))->status, AdapterStatus::Ok);
    };

    ingest(1000);
    ingest(2000);
    ingest(3000);

    auto bounded = std::make_unique<ListBlockMetaRequest>();
    bounded->provider = "p1";
    bounded->symbol = "s1";
    bounded->source = "src";
    bounded->tf = Timeframe::Ticks;
    bounded->from_block_ts = 1500;
    bounded->to_block_ts = 2500;

    auto bounded_resp = adapter.list_block_meta(std::move(bounded));
    CHECK_EQ(bounded_resp->status, AdapterStatus::Ok);
    CHECK_EQ(bounded_resp->blocks.size(), static_cast<std::size_t>(1));
    CHECK_EQ(bounded_resp->blocks[0].key.block_ts, 2000);

    auto unbounded = std::make_unique<ListBlockMetaRequest>();
    unbounded->provider = "p1";
    unbounded->symbol = "s1";
    unbounded->source = "src";
    unbounded->tf = Timeframe::Ticks;
    unbounded->from_block_ts = std::nullopt;
    unbounded->to_block_ts = std::nullopt;

    auto unbounded_resp = adapter.list_block_meta(std::move(unbounded));
    CHECK_EQ(unbounded_resp->status, AdapterStatus::Ok);
    CHECK_EQ(unbounded_resp->blocks.size(), static_cast<std::size_t>(3));
}

void test_list_block_meta_filters_symbol_source_tf() {
    FakeDfhAdapter adapter;

    auto ingest = [&](BlockKey key) {
        auto req = std::make_unique<IngestRequest>();
        req->key = std::move(key);
        req->payload = bytes({5, 5});
        CHECK_EQ(adapter.ingest_structured(std::move(req))->status, AdapterStatus::Ok);
    };

    ingest(make_key(1000, Timeframe::Ticks, "p1", "s1", "src"));
    ingest(make_key(1000, Timeframe::Ticks, "p1", "s2", "src"));
    ingest(make_key(1000, Timeframe::Ticks, "p1", "s1", "other_source"));
    ingest(make_key(1000, Timeframe::M1, "p1", "s1", "src"));

    auto req = std::make_unique<ListBlockMetaRequest>();
    req->provider = "p1";
    req->symbol = "s1";
    req->source = "src";
    req->tf = Timeframe::Ticks;

    auto resp = adapter.list_block_meta(std::move(req));
    CHECK_EQ(resp->status, AdapterStatus::Ok);
    CHECK(resp->error_code.empty());
    CHECK_EQ(resp->blocks.size(), static_cast<std::size_t>(1));
    CHECK_EQ(resp->blocks[0].key.symbol, std::string("s1"));
    CHECK_EQ(resp->blocks[0].key.source, std::string("src"));
    CHECK_EQ(resp->blocks[0].key.tf, Timeframe::Ticks);
}

void test_get_block_hash_happy_path() {
    FakeDfhAdapter adapter;
    const auto payload = bytes({0x10, 0x20, 0x30});

    auto ingest_req = std::make_unique<IngestRequest>();
    ingest_req->key = make_key(444);
    ingest_req->payload = payload;
    CHECK_EQ(adapter.ingest_structured(std::move(ingest_req))->status, AdapterStatus::Ok);

    auto hash_req = std::make_unique<GetBlockHashRequest>();
    hash_req->key = make_key(444);
    auto hash_resp = adapter.get_block_hash(std::move(hash_req));

    std::array<std::uint8_t, 32> expected_hash{};
    const std::string payload_str(reinterpret_cast<const char *>(payload.data()), payload.size());
    compute_sha256_raw(payload_str, expected_hash.data());

    CHECK_EQ(hash_resp->status, AdapterStatus::Ok);
    CHECK(hash_resp->error_code.empty());
    CHECK_EQ(hash_resp->hash.size(), static_cast<std::size_t>(32));
    CHECK_EQ(hash_resp->hash, expected_hash);
}

void test_get_block_hash_not_found() {
    FakeDfhAdapter adapter;

    auto req = std::make_unique<GetBlockHashRequest>();
    req->key = make_key(777);
    auto resp = adapter.get_block_hash(std::move(req));
    const std::array<std::uint8_t, 32> zero_hash{};

    CHECK_EQ(resp->status, AdapterStatus::Error);
    CHECK_EQ(resp->error_code, std::string("not_found"));
    CHECK_EQ(resp->hash, zero_hash);
}

void test_merge_block_dfhbin_happy_path() {
    FakeDfhAdapter adapter;
    const auto payload = bytes({0xaa, 0xbb, 0xcc, 0xdd});

    auto merge_req = std::make_unique<MergeBlockDfhbinRequest>();
    merge_req->key = make_key(888);
    merge_req->bytes = payload;

    auto merge_resp = adapter.merge_block_dfhbin(std::move(merge_req));
    CHECK_EQ(merge_resp->status, AdapterStatus::Ok);
    CHECK(merge_resp->error_code.empty());

    auto get_req = std::make_unique<GetBlockDfhbinRequest>();
    get_req->key = make_key(888);
    auto get_resp = adapter.get_block_dfhbin(std::move(get_req));
    CHECK_EQ(get_resp->status, AdapterStatus::Ok);
    CHECK_EQ(get_resp->payload, payload);

    auto meta_req = std::make_unique<ListBlockMetaRequest>();
    meta_req->provider = "p1";
    meta_req->symbol = "s1";
    meta_req->source = "src";
    meta_req->tf = Timeframe::Ticks;
    auto meta_resp = adapter.list_block_meta(std::move(meta_req));
    CHECK_EQ(meta_resp->status, AdapterStatus::Ok);
    CHECK_EQ(meta_resp->blocks.size(), static_cast<std::size_t>(1));

    std::array<std::uint8_t, 32> expected_hash{};
    const std::string payload_str(reinterpret_cast<const char *>(payload.data()), payload.size());
    compute_sha256_raw(payload_str, expected_hash.data());
    CHECK_EQ(meta_resp->blocks[0].hash, expected_hash);
}

void test_merge_block_dfhbin_invalid_argument() {
    FakeDfhAdapter adapter;

    auto resp = adapter.merge_block_dfhbin(std::unique_ptr<MergeBlockDfhbinRequest>());
    CHECK_EQ(resp->status, AdapterStatus::Error);
    CHECK_EQ(resp->error_code, std::string("invalid_argument"));
}

void test_reset_clears_storage() {
    FakeDfhAdapter adapter;

    auto ingest_req = std::make_unique<IngestRequest>();
    ingest_req->key = make_key(12345);
    ingest_req->payload = bytes({1, 2, 3, 4});
    CHECK_EQ(adapter.ingest_structured(std::move(ingest_req))->status, AdapterStatus::Ok);

    adapter.reset();

    auto get_req = std::make_unique<GetBlockDfhbinRequest>();
    get_req->key = make_key(12345);
    auto get_resp = adapter.get_block_dfhbin(std::move(get_req));
    CHECK_EQ(get_resp->status, AdapterStatus::Error);
    CHECK_EQ(get_resp->error_code, std::string("not_found"));

    auto meta_req = std::make_unique<ListBlockMetaRequest>();
    meta_req->provider = "p1";
    meta_req->symbol = "s1";
    meta_req->source = "src";
    meta_req->tf = Timeframe::Ticks;
    auto meta_resp = adapter.list_block_meta(std::move(meta_req));
    CHECK_EQ(meta_resp->status, AdapterStatus::Ok);
    CHECK(meta_resp->blocks.empty());
}

void test_error_invariants() {
    FakeDfhAdapter adapter;
    const std::array<std::uint8_t, 32> zero_hash{};

    auto history_resp = adapter.query_history(std::unique_ptr<QueryHistoryRequest>());
    CHECK_EQ(history_resp->status, AdapterStatus::Error);
    CHECK(!history_resp->error_code.empty());
    CHECK(history_resp->chunks.empty());

    auto get_resp = adapter.get_block_dfhbin(std::unique_ptr<GetBlockDfhbinRequest>());
    CHECK_EQ(get_resp->status, AdapterStatus::Error);
    CHECK(!get_resp->error_code.empty());
    CHECK(get_resp->payload.empty());

    auto meta_resp = adapter.list_block_meta(std::unique_ptr<ListBlockMetaRequest>());
    CHECK_EQ(meta_resp->status, AdapterStatus::Error);
    CHECK(!meta_resp->error_code.empty());
    CHECK(meta_resp->blocks.empty());

    auto hash_resp = adapter.get_block_hash(std::unique_ptr<GetBlockHashRequest>());
    CHECK_EQ(hash_resp->status, AdapterStatus::Error);
    CHECK(!hash_resp->error_code.empty());
    CHECK_EQ(hash_resp->hash, zero_hash);
}

} // namespace

int main() {
    test_ingest_happy_path();
    test_ingest_overwrite();
    test_ingest_invalid_argument();
    test_query_history_happy_path();
    test_query_history_block_boundaries();
    test_query_history_empty_range();
    test_query_history_filters_and_m1_timeframe();
    test_get_block_dfhbin_happy_path();
    test_get_block_dfhbin_not_found();
    test_list_block_meta_filter();
    test_list_block_meta_bounds();
    test_list_block_meta_filters_symbol_source_tf();
    test_get_block_hash_happy_path();
    test_get_block_hash_not_found();
    test_merge_block_dfhbin_happy_path();
    test_merge_block_dfhbin_invalid_argument();
    test_reset_clears_storage();
    test_error_invariants();
    return 0;
}
