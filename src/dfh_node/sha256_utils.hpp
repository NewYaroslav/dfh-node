/// \file sha256_utils.hpp
/// \brief Утилиты SHA-256 для вычисления hash и проверки в constant-time.
/// \details Предоставляет raw/hex SHA-256 и верификацию hash через CRYPTO_memcmp.
///
#pragma once

#include <string>

namespace dfh_node {

/// \brief Вычисляет SHA-256 hash в raw bytes (32 байта).
/// \param data Входные данные для hash.
/// \param out_32bytes Выходной буфер на 32 байта.
/// \return Ничего не возвращает.
void compute_sha256_raw(const std::string &data, unsigned char *out_32bytes);

/// \brief Вычисляет SHA-256 hash в hex lowercase (64 символа).
/// \param data Входные данные для hash.
/// \return SHA-256 hash в hex lowercase.
std::string compute_sha256_hex(const std::string &data);

/// \brief Проверяет SHA-256 hash через constant-time compare.
/// \param data Входные данные для пересчёта hash.
/// \param expected_hash Ожидаемый hash в hex lowercase (64 символа).
/// \return true, если hash совпадает, иначе false.
bool verify_sha256(const std::string &data, const std::string &expected_hash);

} // namespace dfh_node
