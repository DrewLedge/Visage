#version 460

#extension GL_EXT_ray_tracing : require

#include "../includes/raypayloads.glsl"
layout(location = 1) rayPayloadInEXT ShadowPayload shadowPayload;

void main() {
    shadowPayload.shadow = false;
}
