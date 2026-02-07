/**
 * \file fingerprint_computer.cpp
 * \brief Реализация вычисления fingerprint для API-токенов.
 * \details Использует OpenSSL HMAC-SHA256 и кодирует результат в lowercase hex.
 */
#include "fingerprint_computer.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <array>
#include <stdexcept>

namespace dfh_node {

namespace {

constexpr char k_hex_digits[] = "0123456789abcdef";

} // namespace

FingerprintComputer::FingerprintComputer(const std::string &server_secret)
    : m_server_secret(server_secret) {}

std::string FingerprintComputer::compute(const std::string &token) const {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_size = 0;

    const unsigned char *token_data =
        reinterpret_cast<const unsigned char *>(token.data());
    const auto key_data =
        reinterpret_cast<const unsigned char *>(m_server_secret.data());

    if (HMAC(EVP_sha256(), key_data, static_cast<int>(m_server_secret.size()),
             token_data, token.size(), digest.data(), &digest_size) == nullptr) {
        throw std::runtime_error("HMAC(EVP_sha256) failed");
    }

    std::string result;
    result.reserve(digest_size * 2);
    for (unsigned int i = 0; i < digest_size; ++i) {
        const unsigned char byte = digest[i];
        result.push_back(k_hex_digits[(byte >> 4U) & 0x0FU]);
        result.push_back(k_hex_digits[byte & 0x0FU]);
    }

    return result;
}

} // namespace dfh_node
