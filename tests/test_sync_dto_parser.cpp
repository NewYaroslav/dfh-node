/// \file test_sync_dto_parser.cpp
/// \brief Тесты парсинга DTO для HTTP sync-endpoints.
/// \details Проверяет разбор JSON-фильтра `/sync/meta` и query-параметров
/// `/sync/block`.
///
#include "adapter.hpp"
#include "test_helpers.hpp"
#include "transport.hpp"

#include <string>
#include <variant>

namespace {

void test_parse_sync_meta_body_empty_filter() {
    const auto parsed = dfh_node::transport::parse_sync_meta_body("{}");
    CHECK(std::holds_alternative<dfh_node::ListBlockMetaRequest>(parsed));

    const auto &request = std::get<dfh_node::ListBlockMetaRequest>(parsed);
    CHECK(request.provider.empty());
    CHECK(request.symbol.empty());
    CHECK(request.source.empty());
    CHECK_EQ(request.tf, dfh_node::Timeframe::Ticks);
    CHECK(!request.from_block_ts.has_value());
    CHECK(!request.to_block_ts.has_value());
}

void test_parse_sync_meta_body_with_ticks() {
    const std::string body =
        R"({"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","from_block_ts":"100","to_block_ts":200})";
    const auto parsed = dfh_node::transport::parse_sync_meta_body(body);
    CHECK(std::holds_alternative<dfh_node::ListBlockMetaRequest>(parsed));

    const auto &request = std::get<dfh_node::ListBlockMetaRequest>(parsed);
    CHECK_EQ(request.provider, "binance");
    CHECK_EQ(request.symbol, "BTCUSDT");
    CHECK_EQ(request.source, "spot");
    CHECK_EQ(request.tf, dfh_node::Timeframe::Ticks);
    CHECK(request.from_block_ts.has_value());
    CHECK_EQ(*request.from_block_ts, 100);
    CHECK(request.to_block_ts.has_value());
    CHECK_EQ(*request.to_block_ts, 200);
}

void test_parse_sync_meta_body_unknown_tf() {
    const auto parsed = dfh_node::transport::parse_sync_meta_body(R"({"tf":"unknown"})");
    CHECK(std::holds_alternative<dfh_node::transport::ParseError>(parsed));
    CHECK_EQ(std::get<dfh_node::transport::ParseError>(parsed).first, "invalid_query_param");
}

void test_parse_sync_meta_body_invalid_json() {
    const auto parsed = dfh_node::transport::parse_sync_meta_body("not_json");
    CHECK(std::holds_alternative<dfh_node::transport::ParseError>(parsed));
    CHECK_EQ(std::get<dfh_node::transport::ParseError>(parsed).first, "invalid_json");
}

void test_parse_sync_block_query_success() {
    const auto parsed =
        dfh_node::transport::parse_sync_block_query("provider=X&symbol=Y&source=Z&tf=ticks&block_ts=123");
    CHECK(std::holds_alternative<dfh_node::GetBlockDfhbinRequest>(parsed));

    const auto &request = std::get<dfh_node::GetBlockDfhbinRequest>(parsed);
    CHECK_EQ(request.key.provider, "X");
    CHECK_EQ(request.key.symbol, "Y");
    CHECK_EQ(request.key.source, "Z");
    CHECK_EQ(request.key.tf, dfh_node::Timeframe::Ticks);
    CHECK_EQ(request.key.block_ts, 123);
}

void test_parse_sync_block_query_missing_field() {
    const auto parsed = dfh_node::transport::parse_sync_block_query("provider=X&symbol=Y");
    CHECK(std::holds_alternative<dfh_node::transport::ParseError>(parsed));
    CHECK_EQ(std::get<dfh_node::transport::ParseError>(parsed).first, "invalid_query_param");
}

void test_parse_sync_block_query_bad_tf() {
    const auto parsed = dfh_node::transport::parse_sync_block_query("provider=X&symbol=Y&source=Z&tf=bad&block_ts=123");
    CHECK(std::holds_alternative<dfh_node::transport::ParseError>(parsed));
    CHECK_EQ(std::get<dfh_node::transport::ParseError>(parsed).first, "invalid_query_param");
}

} // namespace

int main() {
    test_parse_sync_meta_body_empty_filter();
    test_parse_sync_meta_body_with_ticks();
    test_parse_sync_meta_body_unknown_tf();
    test_parse_sync_meta_body_invalid_json();
    test_parse_sync_block_query_success();
    test_parse_sync_block_query_missing_field();
    test_parse_sync_block_query_bad_tf();
    return 0;
}
