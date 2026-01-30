#pragma once

#include "dfh_node/config.hpp"

#include <string>
#include <vector>

namespace dfh_node::config {

struct ValidationError {
  std::string path;
  std::string code;
  std::string message;
};

std::vector<ValidationError> validate(const Config& cfg);

}  // namespace dfh_node::config
