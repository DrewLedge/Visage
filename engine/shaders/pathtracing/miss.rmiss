#version 460

#extension GL_EXT_ray_tracing : require

#include "../includes/raypayloads.glsl"
layout(location = 0) rayPayloadInEXT PrimaryPayload payload;

layout(set = 2, binding = 0) uniform samplerCube cubeMap;

void main() {
    payload.terminate = true;
    payload.col = texture(cubeMap, gl_WorldRayDirectionEXT).rgb * 0.01f;
}
