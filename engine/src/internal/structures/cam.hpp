#pragma once

#include "../../libraries/dml.hpp"

namespace cam {
struct CamMatrices {
    dml::mat4 view{};
    dml::mat4 proj{};
    dml::mat4 iview{};
    dml::mat4 iproj{};
};
}  // namespace cam
