#version 460

#extension GL_EXT_ray_tracing : require

#include "../includes/raypayloads.glsl"
layout(location = 0) rayPayloadInEXT PrimaryPayload payload;

layout(set = 2, binding = 0) uniform samplerCube cubeMap;

void main() {
    payload.ray.terminate = true;

    vec3 color = texture(cubeMap, gl_WorldRayDirectionEXT).rgb;
    payload.col += color * payload.throughput;
}
