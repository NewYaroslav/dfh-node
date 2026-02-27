/// \file test_config_defaults.cpp
/// \brief Проверка значений конфигурации по умолчанию.
/// \details Убеждается, что дефолты не меняются без явной правки.
///
#include "config.hpp"
#include "test_helpers.hpp"

int main() {
    // Дефолты должны совпадать с ожиданиями в документации/валидаторе.
    auto cfg = dfh_node::config::default_config();
    CHECK_EQ(cfg.schema_version, 1);
    CHECK_EQ(cfg.http.port, 8080);
    CHECK_EQ(cfg.http.request_timeout_ms, static_cast<std::int64_t>(30000));
    CHECK_EQ(cfg.http.history_max_range_ms, static_cast<std::int64_t>(86400000));
    CHECK_EQ(cfg.http.history_max_bytes, static_cast<std::int64_t>(104857600));
    CHECK_EQ(cfg.ws.port, 8081);
    CHECK(cfg.node_id.empty());
    CHECK(cfg.env.empty());
    CHECK(cfg.security.server_secret.empty());
    CHECK_EQ(cfg.security.anti_replay.require_for_scopes, dfh_node::to_scope_mask(dfh_node::Scope::Write) |
                                                              dfh_node::to_scope_mask(dfh_node::Scope::Admin) |
                                                              dfh_node::to_scope_mask(dfh_node::Scope::Sync));
    return 0;
}
