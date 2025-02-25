#version 460

layout(location = 0) in vec4 inFragPos;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform samplerCube cubeMap;

void main() {
    outColor = texture(cubeMap, inFragPos.xyz);
    outColor.rgb *= 0.01f;
}
