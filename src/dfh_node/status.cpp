#include "dfh_node/interfaces.hpp"

#include <chrono>
#include <random>

namespace dfh_node {

std::uint64_t SystemClock::now_ms() const {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::uint64_t SystemRandom::next_uint64() {
  std::random_device rd;
  const std::uint64_t hi = static_cast<std::uint64_t>(rd());
  const std::uint64_t lo = static_cast<std::uint64_t>(rd());
  return (hi << 32) ^ lo;
}

}  // namespace dfh_node
