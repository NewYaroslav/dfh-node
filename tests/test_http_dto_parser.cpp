/// \file test_http_dto_parser.cpp
/// \brief Тесты парсера HTTP DTO.
/// \details Проверяет разбор `GET /v1/history` и `POST /v1/ingest`, включая лимиты и коды ошибок.
///
#include "config.hpp"
#include "test_helpers.hpp"
#include "transport/http/http_dto_parser.hpp"

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

template <typename T>
const dfh_node::transport::ParseError &expect_parse_error(const dfh_node::transport::ParseResult<T> &result) {
    CHECK(std::holds_alternative<dfh_node::transport::ParseError>(result));
    return std::get<dfh_node::transport::ParseError>(result);
}

void test_parse_valid_history_query() {
    auto cfg = dfh_node::config::default_config();
    const std::string query =
        "?provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1704067200000&to_ms=1704070800000&format=csv"
        "&provider_id=42&symbol_id=7";

    auto result = dfh_node::transport::parse_history_query(query, cfg.http);
    CHECK(std::holds_alternative<dfh_node::QueryHistoryRequest>(result));

    const auto &dto = std::get<dfh_node::QueryHistoryRequest>(result);
    CHECK_EQ(dto.provider, "binance");
    CHECK_EQ(dto.symbol, "BTCUSDT");
    CHECK_EQ(dto.source, "spot");
    CHECK_EQ(dto.tf, dfh_node::Timeframe::Ticks);
    CHECK_EQ(dto.from_ms, static_cast<std::int64_t>(1704067200000));
    CHECK_EQ(dto.to_ms, static_cast<std::int64_t>(1704070800000));
    CHECK(dto.provider_id.has_value());
    CHECK(dto.symbol_id.has_value());
    CHECK_EQ(*dto.provider_id, static_cast<std::uint32_t>(42));
    CHECK_EQ(*dto.symbol_id, static_cast<std::uint32_t>(7));
}

void test_parse_history_missing_required_fields() {
    auto cfg = dfh_node::config::default_config();
    struct Case {
        std::string query;
        std::string missing_name;
    };

    const std::vector<Case> cases = {
        {"symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1&to_ms=2", "provider"},
        {"provider=binance&source=spot&tf=ticks&from_ms=1&to_ms=2", "symbol"},
        {"provider=binance&symbol=BTCUSDT&tf=ticks&from_ms=1&to_ms=2", "source"},
        {"provider=binance&symbol=BTCUSDT&source=spot&from_ms=1&to_ms=2", "tf"},
        {"provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&to_ms=2", "from_ms"},
        {"provider=binance&symbol=BTCUSDT&source=spot&tf=ticks&from_ms=1", "to_ms"},
    };

    for (const auto &item : cases) {
        auto result = dfh_node::transport::parse_history_query(item.query, cfg.http);
        const auto &error = expect_parse_error(result);
        CHECK_EQ(error.first, "invalid_query_param");
        CHECK_NE(error.second.find(item.missing_name), std::string::npos);
    }
}

void test_parse_history_invalid_ranges_and_enums() {
    auto cfg = dfh_node::config::default_config();
    cfg.http.history_max_range_ms = 100;

    {
        auto result = dfh_node::transport::parse_history_query(
            "provider=p&symbol=s&source=x&tf=ticks&from_ms=20&to_ms=20", cfg.http);
        const auto &error = expect_parse_error(result);
        CHECK_EQ(error.first, "invalid_query_param");
    }

    {
        auto result = dfh_node::transport::parse_history_query(
            "provider=p&symbol=s&source=x&tf=ticks&from_ms=0&to_ms=101", cfg.http);
        const auto &error = expect_parse_error(result);
        CHECK_EQ(error.first, "range_too_large");
    }

    {
        auto result =
            dfh_node::transport::parse_history_query("provider=p&symbol=s&source=x&tf=h1&from_ms=0&to_ms=1", cfg.http);
        const auto &error = expect_parse_error(result);
        CHECK_EQ(error.first, "invalid_query_param");
    }

    {
        auto result = dfh_node::transport::parse_history_query(
            "provider=p&symbol=s&source=x&tf=ticks&from_ms=0&to_ms=1&format=json", cfg.http);
        const auto &error = expect_parse_error(result);
        CHECK_EQ(error.first, "invalid_query_param");
    }
}

void test_parse_ingest_invalid_json_and_empty_array() {
    auto cfg = dfh_node::config::default_config();

    {
        auto result = dfh_node::transport::parse_ingest_body("{ this is not json", cfg.http);
        const auto &error = expect_parse_error(result);
        CHECK_EQ(error.first, "invalid_json");
    }

    {
        auto result = dfh_node::transport::parse_ingest_body("[]", cfg.http);
        const auto &error = expect_parse_error(result);
        CHECK_EQ(error.first, "invalid_query_param");
    }
}

void test_parse_ingest_payload_limit() {
    auto cfg = dfh_node::config::default_config();
    cfg.http.max_payload_bytes = 8;

    auto result = dfh_node::transport::parse_ingest_body("[{\"k\":1}]", cfg.http);
    const auto &error = expect_parse_error(result);
    CHECK_EQ(error.first, "payload_too_large");
}

void test_parse_ingest_valid_body() {
    auto cfg = dfh_node::config::default_config();
    const std::string body =
        R"([{"key":{"provider":"binance","symbol":"BTCUSDT","source":"spot","tf":"ticks","block_ts":1704067200000},)"
        R"("payload_base64":"AQID"}])";

    auto result = dfh_node::transport::parse_ingest_body(body, cfg.http);
    CHECK(std::holds_alternative<std::vector<dfh_node::IngestRequest>>(result));

    const auto &requests = std::get<std::vector<dfh_node::IngestRequest>>(result);
    CHECK_EQ(requests.size(), static_cast<std::size_t>(1));
    CHECK_EQ(requests[0].key.provider, "binance");
    CHECK_EQ(requests[0].key.symbol, "BTCUSDT");
    CHECK_EQ(requests[0].key.source, "spot");
    CHECK_EQ(requests[0].key.tf, dfh_node::Timeframe::Ticks);
    CHECK_EQ(requests[0].key.block_ts, static_cast<std::int64_t>(1704067200000));
    CHECK_EQ(requests[0].payload.size(), static_cast<std::size_t>(3));
    CHECK_EQ(requests[0].payload[0], static_cast<std::uint8_t>(1));
    CHECK_EQ(requests[0].payload[1], static_cast<std::uint8_t>(2));
    CHECK_EQ(requests[0].payload[2], static_cast<std::uint8_t>(3));
}

} // namespace

int main() {
    test_parse_valid_history_query();
    test_parse_history_missing_required_fields();
    test_parse_history_invalid_ranges_and_enums();
    test_parse_ingest_invalid_json_and_empty_array();
    test_parse_ingest_payload_limit();
    test_parse_ingest_valid_body();
    return 0;
}
