#pragma once

#include <cstdint>

#include "dfh_node/status.hpp"

namespace dfh_node {

class IClock {
  public:
    virtual ~IClock() = default;
    virtual std::uint64_t now_ms() const = 0;
};

class IRandom {
  public:
    virtual ~IRandom() = default;
    virtual std::uint64_t next_uint64() = 0;
};

class IStatusProvider {
  public:
    virtual ~IStatusProvider() = default;
    virtual StatusSnapshot snapshot() const = 0;
};

class SystemClock final : public IClock {
  public:
    std::uint64_t now_ms() const override;
};

class SystemRandom final : public IRandom {
  public:
    std::uint64_t next_uint64() override;
};

} // namespace dfh_node
