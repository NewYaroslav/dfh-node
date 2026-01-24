#include <iostream>

#include "dfh_node/version.hpp"

int main() {
  std::cout << dfh_node::name() << " " << dfh_node::version() << "\n";
  std::cout << "starting...\n";
  return 0;
}
