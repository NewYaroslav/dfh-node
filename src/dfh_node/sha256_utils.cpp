/// \file sha256_utils.cpp
/// \brief Реализация утилит SHA-256.
/// \details Использует OpenSSL EVP для вычисления hash и CRYPTO_memcmp для сравнения.
///
#include "sha256_utils.hpp"

#include <array>
#include <iomanip>
#include <sstream>

#include <openssl/crypto.h>
#include <openssl/evp.h>

namespace dfh_node {

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

} // namespace dfh_node
