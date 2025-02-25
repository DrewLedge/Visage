#pragma once

#include <string>

namespace cfg {
constexpr uint32_t MAX_OBJECTS = 5000;

constexpr uint32_t MAX_LIGHTS = 100;
constexpr uint32_t LIGHTS_PER_BATCH = 3;
constexpr uint32_t MAX_LIGHT_BATCHES = MAX_LIGHTS / LIGHTS_PER_BATCH + 1;

constexpr float NEAR_PLANE = 0.1f;
constexpr float FAR_PLANE = 100.0f;

constexpr uint32_t SCREEN_WIDTH = 2560;

constexpr uint32_t SHADOW_WIDTH = 2048;
constexpr uint32_t SHADOW_HEIGHT = 2048;

constexpr uint32_t SCREEN_HEIGHT = 1600;

const std::string ENGINE_VER = "v0.1.0";

const std::string SOURCE_DIR(PROJECT_SOURCE_DIR);
const std::string SHADER_DIR = SOURCE_DIR + "/shaders/compiled/";
const std::string MODEL_DIR = SOURCE_DIR + "/assets/models/";
const std::string SKYBOX_DIR = SOURCE_DIR + "/assets/skyboxes/";
const std::string FONT_DIR = SOURCE_DIR + "/assets/fonts/";
}  // namespace cfg
