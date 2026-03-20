/// \file transport_security_utils.cpp
/// \brief Реализация transport-утилит для auth и anti-replay.
/// \details Убирает дублирование разбора заголовков между HTTP Admin/Ops,
/// основным HTTP router и WS upgrade.

#include "transport_security_utils.hpp"

#include <cctype>
#include <string_view>
#include <utility>

namespace dfh_node::transport {

std::string trim_copy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string extract_bearer_token(const SimpleWeb::CaseInsensitiveMultimap &headers) {
    const auto auth_it = headers.find("Authorization");
    if (auth_it == headers.end()) {
        return {};
    }

    const std::string auth_header = trim_copy(auth_it->second);
    constexpr std::string_view prefix = "Bearer ";
    if (auth_header.size() < prefix.size() || auth_header.compare(0, prefix.size(), prefix) != 0) {
        return {};
    }

    return trim_copy(std::string_view(auth_header).substr(prefix.size()));
}

const HttpAntiReplayFields *parse_http_anti_replay_fields(const std::string &method, const std::string &path,
                                                          const std::string &query_string,
                                                          const SimpleWeb::CaseInsensitiveMultimap &headers,
                                                          const std::string &body_hash,
                                                          HttpAntiReplayFields &fields_out, bool &has_any_headers) {
    has_any_headers = false;

    std::string timestamp;
    std::string nonce;
    std::string signature;

    const auto ts_it = headers.find("X-DFH-Timestamp");
    if (ts_it != headers.end()) {
        timestamp = trim_copy(ts_it->second);
        has_any_headers = true;
    }

    const auto nonce_it = headers.find("X-DFH-Nonce");
    if (nonce_it != headers.end()) {
        nonce = trim_copy(nonce_it->second);
        has_any_headers = true;
    }

    const auto sig_it = headers.find("X-DFH-Signature");
    if (sig_it != headers.end()) {
        signature = trim_copy(sig_it->second);
        has_any_headers = true;
    }

    if (!has_any_headers) {
        return nullptr;
    }

    if (timestamp.empty() || nonce.empty() || signature.empty()) {
        return nullptr;
    }

    fields_out.method = method;
    fields_out.path = path;
    fields_out.query_params.clear();

    const std::string_view query = (!query_string.empty() && query_string.front() == '?')
                                       ? std::string_view(query_string).substr(1)
                                       : std::string_view(query_string);

    std::size_t start = 0;
    while (start <= query.size()) {
        const std::size_t amp = query.find('&', start);
        const std::string_view token =
            (amp == std::string_view::npos) ? query.substr(start) : query.substr(start, amp - start);

        if (!token.empty()) {
            const std::size_t eq = token.find('=');
            const std::string key = std::string(token.substr(0, eq));
            const std::string value =
                (eq == std::string_view::npos) ? std::string() : std::string(token.substr(eq + 1));
            fields_out.query_params.emplace_back(key, value);
        }

        if (amp == std::string_view::npos) {
            break;
        }
        start = amp + 1;
    }

    fields_out.timestamp = std::move(timestamp);
    fields_out.nonce = std::move(nonce);
    fields_out.signature = std::move(signature);
    fields_out.body_hash = body_hash;
    return &fields_out;
}

} // namespace dfh_node::transport
