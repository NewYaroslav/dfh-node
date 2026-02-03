#include "dfh_node/config_loader.hpp"
#include "test_helpers.hpp"

#include <filesystem>
#include <fstream>

int main() {
    {
        auto config_path = std::filesystem::path(__FILE__).parent_path() /
                           ".." / "examples" / "config_minimal.json";
        auto result = dfh_node::config::load_from_file(config_path);
        CHECK(result.is_ok());
        CHECK(result.config.has_value());
        CHECK_EQ(result.config->node_id, "node-01");
    }

    {
        auto result = dfh_node::config::load_from_file("missing_config.json");
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(!result.errors.empty());
        CHECK_EQ(result.errors[0].code, "file_not_found");
    }

    {
        auto temp_path = std::filesystem::temp_directory_path() /
                         "dfh_node_invalid_config.json";
        {
            std::ofstream out(temp_path, std::ios::out | std::ios::binary);
            out << "{";
        }
        auto result = dfh_node::config::load_from_file(temp_path);
        CHECK(!result.is_ok());
        CHECK(!result.config.has_value());
        CHECK(!result.errors.empty());
        CHECK_EQ(result.errors[0].code, "parse_error");
        std::error_code ec;
        std::filesystem::remove(temp_path, ec);
    }

    return 0;
}
