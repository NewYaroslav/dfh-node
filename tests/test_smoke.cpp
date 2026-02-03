#include <cassert>

#include "dfh_node/version.hpp"

int main() {
    assert(dfh_node::version() == dfh_node::kVersion);
    assert(dfh_node::name() == dfh_node::kNodeName);
    return 0;
}
