#pragma once

#include <string_view>

namespace dfh_node {

constexpr std::string_view kVersion = "0.1.0";
constexpr std::string_view kNodeName = "dfh-node";

std::string_view version();
std::string_view name();

}  // namespace dfh_node
