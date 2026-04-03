/// \file sha256_utils.cpp
/// \brief Реализация утилит SHA-256.
/// \details Использует OpenSSL EVP для вычисления hash и CRYPTO_memcmp для сравнения.
///
#include "sha256_utils.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace dfh_node {
namespace {

int hex_char_to_value(const char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return (ch - 'a') + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return (ch - 'A') + 10;
    }
    return -1;
}

} // namespace

void compute_sha256_raw(const std::string &data, unsigned char *out_32bytes) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data.data(), data.size());
    unsigned int hash_size = 0;
    EVP_DigestFinal_ex(ctx, out_32bytes, &hash_size);
    EVP_MD_CTX_free(ctx);
}

std::string compute_sha256_hex(const std::string &data) {
    std::array<unsigned char, 32> hash{};
    compute_sha256_raw(data, hash.data());

    std::ostringstream output;
    for (unsigned char byte : hash) {
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return output.str();
}

bool verify_sha256(const std::string &data, const std::string &expected_hash) {
    if (expected_hash.size() != 64) {
        return false;
    }

    const std::string actual_hash = compute_sha256_hex(data);
    return CRYPTO_memcmp(actual_hash.data(), expected_hash.data(), 64) == 0;
}

std::vector<std::uint8_t> hex_to_bytes(const std::string_view hex) {
    std::vector<std::uint8_t> bytes;
    if ((hex.size() % 2U) != 0U) {
        return bytes;
    }

    bytes.reserve(hex.size() / 2U);
    for (std::size_t index = 0; index < hex.size(); index += 2U) {
        const int hi = hex_char_to_value(hex[index]);
        const int lo = hex_char_to_value(hex[index + 1U]);
        if (hi < 0 || lo < 0) {
            bytes.clear();
            return bytes;
        }

        bytes.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }

    return bytes;
}

} // namespace dfh_node
