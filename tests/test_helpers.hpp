#pragma once

#include <iostream>
#include <cstdlib>

#define CHECK(condition) \
  do { \
    if (!(condition)) { \
      std::cerr << "CHECK FAILED: " << #condition \
                << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
      std::exit(1); \
    } \
  } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))
#define CHECK_NE(a, b) CHECK((a) != (b))
