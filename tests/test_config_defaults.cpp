#include "dfh_node/config.hpp"
#include "test_helpers.hpp"

int main() {
  auto cfg = dfh_node::config::default_config();
  CHECK_EQ(cfg.schema_version, 1);
  CHECK_EQ(cfg.http.port, 8080);
  CHECK_EQ(cfg.ws.port, 8081);
  CHECK(cfg.node_id.empty());
  CHECK(cfg.env.empty());
  CHECK(cfg.security.server_secret.empty());
  return 0;
}
