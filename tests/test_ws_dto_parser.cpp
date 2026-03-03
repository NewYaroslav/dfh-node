/// \file test_ws_dto_parser.cpp
/// \brief Unit-тесты парсинга WS payload в DTO адаптера.
/// \details Проверяет разбор `history`, `ingest` и `dfhbin` payload, а также
/// типовые ошибки валидации.
///
#include "config.hpp"
#include "test_helpers.hpp"
#include "transport/ws/ws_dto_parser.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

using dfh_node::transport::ParseError;

template <typename T> const ParseError *as_error(const dfh_node::transport::ParseResult<T> &result) {
    return std::get_if<ParseError>(&result);
}

void test_parse_history_valid() {
    const auto cfg = dfh_node::config::default_config();
    const nlohmann::json payload = {
        {"provider", "binance"}, {"symbol", "BTCUSDT"},        {"source", "spot"},
        {"tf", "ticks"},         {"from_ms", 1704067200000LL}, {"to_ms", 1704070800000LL},
    };

    const auto parsed = dfh_node::transport::parse_ws_history_payload(payload, cfg.ws);
    const auto *dto = std::get_if<dfh_node::QueryHistoryRequest>(&parsed);
    CHECK(dto != nullptr);
    CHECK_EQ(dto->provider, "binance");
    CHECK_EQ(dto->symbol, "BTCUSDT");
    CHECK_EQ(dto->source, "spot");
    CHECK_EQ(dto->tf, dfh_node::Timeframe::Ticks);
    CHECK_EQ(dto->from_ms, 1704067200000LL);
    CHECK_EQ(dto->to_ms, 1704070800000LL);
}

void test_parse_history_errors() {
    const auto cfg = dfh_node::config::default_config();
    {
        const nlohmann::json payload = {
            {"provider", "binance"}, {"symbol", "BTCUSDT"},      {"source", "spot"},
            {"tf", "ticks"},         {"to_ms", 1704070800000LL},
        };
        const auto parsed = dfh_node::transport::parse_ws_history_payload(payload, cfg.ws);
        const auto *error = as_error(parsed);
        CHECK(error != nullptr);
        CHECK_EQ(error->first, "invalid_argument");
        CHECK_NE(error->second.find("from_ms"), std::string::npos);
    }

    {
        const nlohmann::json payload = {
            {"provider", "binance"}, {"symbol", "BTCUSDT"},        {"source", "spot"},
            {"tf", "ticks"},         {"from_ms", 1704070800000LL}, {"to_ms", 1704070800000LL},
        };
        const auto parsed = dfh_node::transport::parse_ws_history_payload(payload, cfg.ws);
        const auto *error = as_error(parsed);
        CHECK(error != nullptr);
        CHECK_EQ(error->first, "invalid_argument");
        CHECK_NE(error->second.find("from_ms must be < to_ms"), std::string::npos);
    }
}

void test_parse_ingest_valid() {
    auto cfg = dfh_node::config::default_config();
    cfg.ws.max_payload_bytes = 1024;
    const nlohmann::json payload = {
        {"provider", "binance"}, {"symbol", "BTCUSDT"},         {"source", "spot"},
        {"tf", "ticks"},         {"block_ts", 1704067200000LL}, {"payload_base64", "AQIDBA=="}};

    const auto parsed = dfh_node::transport::parse_ws_ingest_payload(payload, cfg.ws);
    const auto *dto = std::get_if<dfh_node::IngestRequest>(&parsed);
    CHECK(dto != nullptr);
    CHECK_EQ(dto->key.provider, "binance");
    CHECK_EQ(dto->key.symbol, "BTCUSDT");
    CHECK_EQ(dto->key.source, "spot");
    CHECK_EQ(dto->key.tf, dfh_node::Timeframe::Ticks);
    CHECK_EQ(dto->key.block_ts, 1704067200000LL);
    CHECK_EQ(dto->payload.size(), static_cast<std::size_t>(4));
}

void test_parse_ingest_empty_provider() {
    const auto cfg = dfh_node::config::default_config();
    const nlohmann::json payload = {{"provider", ""}, {"symbol", "BTCUSDT"},         {"source", "spot"},
                                    {"tf", "ticks"},  {"block_ts", 1704067200000LL}, {"payload_base64", "AQID"}};

    const auto parsed = dfh_node::transport::parse_ws_ingest_payload(payload, cfg.ws);
    const auto *error = as_error(parsed);
    CHECK(error != nullptr);
    CHECK_EQ(error->first, "invalid_argument");
    CHECK_NE(error->second.find("provider"), std::string::npos);
}

void test_parse_dfhbin_valid_and_error() {
    {
        const nlohmann::json payload = {
            {"provider", "binance"},       {"symbol", "BTCUSDT"}, {"source", "spot"}, {"tf", "m1"},
            {"block_ts", 1704067200000LL},
        };
        const auto parsed = dfh_node::transport::parse_ws_dfhbin_payload(payload);
        const auto *dto = std::get_if<dfh_node::BlockKey>(&parsed);
        CHECK(dto != nullptr);
        CHECK_EQ(dto->provider, "binance");
        CHECK_EQ(dto->symbol, "BTCUSDT");
        CHECK_EQ(dto->source, "spot");
        CHECK_EQ(dto->tf, dfh_node::Timeframe::M1);
        CHECK_EQ(dto->block_ts, 1704067200000LL);
    }

    {
        const nlohmann::json payload = {
            {"provider", "binance"},
            {"symbol", "BTCUSDT"},
            {"source", "spot"},
            {"block_ts", 1704067200000LL},
        };
        const auto parsed = dfh_node::transport::parse_ws_dfhbin_payload(payload);
        const auto *error = as_error(parsed);
        CHECK(error != nullptr);
        CHECK_EQ(error->first, "invalid_argument");
        CHECK_NE(error->second.find("tf"), std::string::npos);
    }
}

} // namespace

int main() {
    test_parse_history_valid();
    test_parse_history_errors();
    test_parse_ingest_valid();
    test_parse_ingest_empty_provider();
    test_parse_dfhbin_valid_and_error();
    return 0;
}
