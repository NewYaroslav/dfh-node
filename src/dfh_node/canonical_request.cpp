/// \file canonical_request.cpp
/// \brief Реализация канонизации HTTP/WS запросов и подписи HMAC-SHA256.
/// \details Содержит RFC3986-канонизацию query и константное время проверку подписи.
///
#include "canonical_request.hpp"

#include "sha256_utils.hpp"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace dfh_node {
namespace {

/// Декодирует percent-encoding; символ '+' трактуется как пробел.
std::string percent_decode(const std::string &value) {
    std::string decoded;
    decoded.reserve(value.size());

    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '+') {
            decoded.push_back(' ');
            continue;
        }

        if (value[i] == '%' && i + 2 < value.size()) {
            const std::string hex = value.substr(i + 1, 2);
            std::istringstream stream(hex);
            int byte_value = 0;
            stream >> std::hex >> byte_value;
            if (!stream.fail()) {
                decoded.push_back(static_cast<char>(byte_value));
                i += 2;
                continue;
            }
        }

        decoded.push_back(value[i]);
    }

    return decoded;
}

/// Кодирует строку по RFC3986 с hex в верхнем регистре.
std::string percent_encode(const std::string &value) {
    std::ostringstream stream;
    for (const unsigned char ch : value) {
        if (std::isalnum(ch) != 0 || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            stream << ch;
            continue;
        }

        stream << '%' << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(ch);
    }
    return stream.str();
}

} // namespace

std::string canonicalize_query_string(const std::vector<std::pair<std::string, std::string>> &params) {
    if (params.empty()) {
        return "";
    }

    std::vector<std::pair<std::string, std::string>> decoded_params;
    decoded_params.reserve(params.size());

    for (const auto &param : params) {
        decoded_params.emplace_back(percent_decode(param.first), percent_decode(param.second));
    }

    std::sort(decoded_params.begin(), decoded_params.end());

    std::ostringstream stream;
    bool is_first = true;
    for (const auto &param : decoded_params) {
        if (!is_first) {
            stream << '&';
        }
        stream << percent_encode(param.first) << '=' << percent_encode(param.second);
        is_first = false;
    }

    return stream.str();
}

std::string canonicalize_http(const HttpCanonicalInput &input) {
    std::ostringstream stream;
    stream << input.method << '\n'
           << input.path << '\n'
           << canonicalize_query_string(input.query_params) << '\n'
           << input.timestamp << '\n'
           << input.nonce << '\n'
           << input.body_hash;
    return stream.str();
}

std::string canonicalize_ws(const WsCanonicalInput &input) {
    std::ostringstream stream;
    stream << input.endpoint << '\n'
           << input.op << '\n'
           << input.msg_id << '\n'
           << input.timestamp << '\n'
           << input.nonce << '\n'
           << input.payload_hash;
    return stream.str();
}

std::string compute_signature(const std::string &canonical, const unsigned char *signing_key, std::size_t key_len) {
    unsigned int hmac_len = 0;
    unsigned char hmac_result[EVP_MAX_MD_SIZE] = {};

    HMAC(EVP_sha256(), signing_key, static_cast<int>(key_len),
         reinterpret_cast<const unsigned char *>(canonical.data()), static_cast<std::size_t>(canonical.size()),
         hmac_result, &hmac_len);

    std::ostringstream stream;
    for (unsigned int i = 0; i < hmac_len; ++i) {
        stream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hmac_result[i]);
    }
    return stream.str();
}

bool verify_signature(const std::string &canonical, const std::string &expected_signature,
                      const unsigned char *signing_key, std::size_t key_len) {
    if (expected_signature.size() != 64) {
        return false;
    }

    const std::string actual_signature = compute_signature(canonical, signing_key, key_len);
    return CRYPTO_memcmp(actual_signature.data(), expected_signature.data(), 64) == 0;
}

std::string compute_signing_key_hex(const std::string &token) { return compute_sha256_hex(token); }

} // namespace dfh_node
