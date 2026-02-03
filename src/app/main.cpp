#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "dfh_node/build_info.hpp"
#include "dfh_node/config_loader.hpp"
#include "dfh_node/config_validator.hpp"
#include "dfh_node/logging.hpp"
#include "dfh_node/status.hpp"
#include "dfh_node/version.hpp"

namespace {

void print_usage() { std::cerr << "Usage: dfh_node_app --config <path>\n"; }

std::string find_config_path(int argc, char **argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) {
                return {};
            }
            return argv[i + 1];
        }
    }
    return {};
}

void print_errors(const std::vector<dfh_node::config::LoadError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": "
                  << err.message << "\n";
    }
}

void print_errors(
    const std::vector<dfh_node::config::ValidationError> &errors) {
    std::cerr << "Configuration errors:\n";
    for (const auto &err : errors) {
        std::cerr << "- [" << err.path << "] " << err.code << ": "
                  << err.message << "\n";
    }
}

} // namespace

int main(int argc, char **argv) {
    const std::string config_path = find_config_path(argc, argv);
    if (config_path.empty()) {
        print_usage();
        return 1;
    }

    const auto result =
        dfh_node::config::load_from_file(std::filesystem::path(config_path));
    if (!result.is_ok()) {
        print_errors(result.errors);
        return 1;
    }

    const auto validation_errors = dfh_node::config::validate(*result.config);
    if (!validation_errors.empty()) {
        print_errors(validation_errors);
        return 1;
    }

    dfh_node::logging::init_logging(result.config->logging);

    LOG_INFO("dfh-node v%s starting...",
             std::string(dfh_node::version()).c_str());
    LOG_INFO("Node ID: %s", result.config->node_id.c_str());
    LOG_INFO("Environment: %s", result.config->env.c_str());

    dfh_node::StatusSnapshot status;
    status.node_id = result.config->node_id;
    status.version = std::string(dfh_node::version());
    status.build_info = dfh_node::build_info_string();
    status.uptime_ms = 0;
    status.peers_count = result.config->peers.size();
    status.env = result.config->env;

    LOG_INFO("Status: node_id=%s, version=%s, build=%s, uptime_ms=%llu, "
             "peers_count=%llu, env=%s",
             status.node_id.c_str(), status.version.c_str(),
             status.build_info.c_str(),
             static_cast<unsigned long long>(status.uptime_ms),
             static_cast<unsigned long long>(status.peers_count),
             status.env.c_str());

    return 0;
}
