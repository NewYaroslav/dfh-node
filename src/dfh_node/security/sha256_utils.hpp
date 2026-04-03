/// \file sha256_utils.hpp
/// \brief Утилиты SHA-256 для вычисления хеша и проверки за константное время.
/// \details Предоставляет SHA-256 в сыром и hex-виде, а также проверку через `CRYPTO_memcmp`.
///
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dfh_node {

/// \brief Вычисляет SHA-256 хеш в сырые байты (32 байта).
/// \param data Входные данные для хеширования.
/// \param out_32bytes Выходной буфер на 32 байта.
/// \return Ничего не возвращает.
void compute_sha256_raw(const std::string &data, unsigned char *out_32bytes);

/// \brief Вычисляет SHA-256 хеш в hex в нижнем регистре (64 символа).
/// \param data Входные данные для хеширования.
/// \return SHA-256 хеш в hex в нижнем регистре.
std::string compute_sha256_hex(const std::string &data);

/// \brief Проверяет SHA-256 хеш сравнением за константное время.
/// \param data Входные данные для пересчёта хеша.
/// \param expected_hash Ожидаемый хеш в hex в нижнем регистре (64 символа).
/// \return true, если хеш совпадает, иначе false.
bool verify_sha256(const std::string &data, const std::string &expected_hash);

/// \brief Декодирует hex-строку в массив байт.
/// \param hex Hex-строка в нижнем или верхнем регистре с чётной длиной.
/// \return Вектор байт; пустой вектор возвращается и для пустой строки, и при
/// невалидном вводе.
std::vector<std::uint8_t> hex_to_bytes(std::string_view hex);

} // namespace dfh_node
