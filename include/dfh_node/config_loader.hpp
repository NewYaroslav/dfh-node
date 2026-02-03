#pragma once

#include "dfh_node/config.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace dfh_node::config {

struct LoadError {
    std::string path;
    std::string code;
    std::string message;
};

struct LoadResult {
    std::optional<Config> config;
    std::vector<LoadError> errors;

    bool is_ok() const { return config.has_value() && errors.empty(); }
};

LoadResult load_from_file(const std::filesystem::path &path);

} // namespace dfh_node::config
