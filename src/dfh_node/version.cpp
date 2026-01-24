#include "dfh_node/version.hpp"

namespace dfh_node {

std::string_view version() {
  return kVersion;
}

std::string_view name() {
  return kNodeName;
}

}  // namespace dfh_node
