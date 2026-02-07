/**
 * \file test_fingerprint_computer.cpp
 * \brief Unit-тесты для FingerprintComputer.
 * \details Проверяет детерминированность, формат и различие fingerprint у
 * разных токенов.
 */
#include "fingerprint_computer.hpp"
#include "test_helpers.hpp"

using namespace dfh_node;

void test_determinism() {
    FingerprintComputer computer("test-secret");
    const std::string fp1 = computer.compute("token123");
    const std::string fp2 = computer.compute("token123");
    CHECK_EQ(fp1, fp2);
}

void test_length_and_format() {
    FingerprintComputer computer("test-secret");
    const std::string fp = computer.compute("token123");
    CHECK_EQ(fp.size(), 64U);
    for (const char ch : fp) {
        CHECK((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
    }
}

void test_different_tokens() {
    FingerprintComputer computer("test-secret");
    const std::string fp1 = computer.compute("token1");
    const std::string fp2 = computer.compute("token2");
    CHECK_NE(fp1, fp2);
}

int main() {
    test_determinism();
    test_length_and_format();
    test_different_tokens();
    return 0;
}
