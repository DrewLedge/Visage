#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform sampler2D texSamplers[];

#include "../includes/texindices.glsl"

layout(set = 1, binding = 0) readonly buffer TexIndexBuffer {
    TexIndices texIndices[];
};

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in mat3 inTBN;  // uses locations 1, 2 and 3
layout(location = 4) flat in uint inObjectIndex;

layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outMetallicRoughness;
layout(location = 2) out vec4 outNormal;
layout(location = 3) out vec4 outEmissiveAO;

#include "../includes/loadtextures.glsl"

void main() {
    // load the textures
    vec4 albedo;
    vec4 metallicRoughness;
    vec3 normal;
    vec3 emissive;
    float occlusion;

    getTextures(texIndices[inObjectIndex], inTexCoord, inTBN, albedo, metallicRoughness, normal, emissive, occlusion);

    // discard if translucent
    if (albedo.a < 0.95f) discard;

    // convert to 0 to 1 range
    normal = normal * 0.5f + 0.5f;

    outAlbedo = vec4(albedo.rgb, 1.0f);
    outMetallicRoughness = vec4(metallicRoughness.rgb, 1.0f);
    outNormal = vec4(normal, 1.0f);
    outEmissiveAO = vec4(emissive, occlusion);
}
