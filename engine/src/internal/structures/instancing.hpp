#pragma once

#include <array>

#include "../../config.hpp"
#include "../../libraries/dml.hpp"

namespace instancing {
struct ObjectInstance {
    dml::mat4 model{};
    uint32_t objectIndex = 0;
};

struct ObjectInstanceData {
    std::array<ObjectInstance, cfg::MAX_OBJECTS> object{};
};
}  // namespace instancing
