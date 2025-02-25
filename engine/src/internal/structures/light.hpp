#pragma once

#include <array>

#include "../../config.hpp"
#include "../../libraries/dml.hpp"

namespace light {
struct LightDataObject {
    dml::vec3 pos{};
    dml::vec3 col{1.0f, 1.0f, 1.0f};
    dml::vec3 target{};

    dml::mat4 viewProj{};

    float intensity = 1.0f;
    float innerConeAngle = 0.23f;
    float outerConeAngle = 0.348f;
    float constantAttenuation = 1.0f;
    float linearAttenuation = 0.1f;
    float quadraticAttenuation = 0.032f;
};

struct RawLights {
    std::array<LightDataObject, cfg::MAX_LIGHTS> raw{};
};
}  // namespace light
