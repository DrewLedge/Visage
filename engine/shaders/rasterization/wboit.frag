#version 460

#extension GL_EXT_nonuniform_qualifier : require

#define RASTERIZATION
#define FRAG_SHADER

layout(set = 0, binding = 0) uniform sampler2D texSamplers[];

layout(set = 2, binding = 0) uniform sampler2DArrayShadow shadowMapSamplers[];

layout(set = 4, binding = 0) uniform sampler2D depthSamplers[];

#include "../includes/helper.glsl"

layout(set = 1, binding = 0) readonly buffer LightBuffer {
    LightData lights[];
}
lssbo[];

layout(set = 5, binding = 0) readonly buffer TexIndexBuffer {
    TexIndices texIndices[];
};

#include "../includes/lightingcalc.glsl"

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in vec3 inFragPos;
layout(location = 2) in vec3 inViewDir;
layout(location = 3) in mat3 inTBN;  // uses locations 3, 4 and 5
layout(location = 6) in float inFarPlane;
layout(location = 7) in float inNearPlane;
layout(location = 8) flat in int inFrame;
layout(location = 9) flat in uint inObjectIndex;

layout(location = 0) out vec4 outColor;

layout(push_constant, std430) uniform pc {
    layout(offset = 4) int lightCount;
    int frameCount;
    int lightsPerBatch;
};

float getWeight(float z, float a) {
    float weight = a * exp(-z);
    return 1.0f - weight;
}

void main() {
    // load the textures
    vec4 albedo;
    vec4 metallicRoughness;
    vec3 normal;
    vec3 emissive;
    float occlusion;

    getTextures(texIndices[inObjectIndex], inTexCoord, inTBN, albedo, metallicRoughness, normal, emissive, occlusion);

    // discard if opaque
    if (albedo.a >= 0.95f) discard;

    // get the depth from the opaque texture
    vec2 coords = getTexCoords(depthSamplers[inFrame], gl_FragCoord.xy);
    float oDepth = texture(depthSamplers[inFrame], coords).r;
    oDepth = linDepth(oDepth, inNearPlane, inFarPlane);

    // get the depth of the fragment
    float tDepth = linDepth(gl_FragCoord.z, inNearPlane, inFarPlane);

    // if the translucent depth is greater than the opaque depth, discard
    if (tDepth > oDepth) discard;

    vec4 color = calcLighting(albedo, metallicRoughness, normal, emissive, occlusion, inFragPos, inViewDir, inFrame, frameCount, lightCount, lightsPerBatch);

    // get the weight and output the color and alpha
    float weight = getWeight(gl_FragCoord.z, color.a);
    outColor = vec4(color.rgb * weight, weight);
}
