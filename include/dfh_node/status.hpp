#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dfh_node {

struct StatusSnapshot {
  std::string node_id;
  std::string version;
  std::string build_info;
  std::uint64_t uptime_ms = 0;
  std::size_t peers_count = 0;
  std::string env;
};

}  // namespace dfh_node
