#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

layout(set = 0, binding = 0) uniform sampler2D texSamplers[];

layout(push_constant, std430) uniform pc {
    int frame;
    int lightCount;
    uint frameCount;
};

#include "../includes/light.glsl"
layout(set = 1, binding = 0) readonly buffer LightBuffer {
    LightData lights[];
}
lssbo[];

layout(set = 5, binding = 0) uniform accelerationStructureEXT TLAS[];

#include "../includes/texindices.glsl"
layout(set = 6, binding = 0) readonly buffer TexIndexBuffer {
    TexIndices texIndices[];
};

#include "../includes/meshdata.glsl"
#include "../includes/raypayloads.glsl"

layout(location = 0) rayPayloadInEXT PrimaryPayload payload;
layout(location = 1) rayPayloadEXT ShadowPayload shadowPayload;
hitAttributeEXT vec2 hit;

void getVertData(uint index, out vec2 uv, out vec3 normal, out vec3 tangent) {
    uint64_t vertAddr = texIndices[gl_InstanceCustomIndexEXT].vertexAddress;
    uint64_t indexAddr = texIndices[gl_InstanceCustomIndexEXT].indexAddress;

    IndexBuffer indexBuffer = IndexBuffer(indexAddr);
    VertBuffer vertBuffer = VertBuffer(vertAddr);

    uint i1 = indexBuffer.indices[index + 0];
    uint i2 = indexBuffer.indices[index + 1];
    uint i3 = indexBuffer.indices[index + 2];

    uint[3] indices = uint[3](i1, i2, i3);
    vec2[3] uvs;
    vec3[3] normals;
    vec3[3] tangents;

    for (uint i = 0; i < 3; i++) {
        uvs[i] = vertBuffer.vertices[indices[i]].tex;
        normals[i] = vertBuffer.vertices[indices[i]].normal;
        tangents[i] = vertBuffer.vertices[indices[i]].tangent;
    }

    float u = hit.x;
    float v = hit.y;

    uv = barycentricvec2(uvs[0], uvs[1], uvs[2], u, v);
    normal = barycentricvec3(normals[0], normals[1], normals[2], u, v);
    tangent = barycentricvec3(tangents[0], tangents[1], tangents[2], u, v);
}

#include "../includes/helper.glsl"
#include "../includes/lightingcalc.glsl"
#include "../includes/loadtextures.glsl"

bool isShadowed(vec3 hitPos, vec3 lightPos, vec3 fragLightDir) {
    const float minDist = 0.01f;
    float maxDist = distance(lightPos, hitPos) - minDist;

    traceRayEXT(
        TLAS[frame],
        gl_RayFlagsOpaqueEXT | gl_RayFlagsTerminateOnFirstHitEXT,
        0xFF,
        1,
        0,
        1,
        hitPos,
        minDist,
        fragLightDir,
        maxDist,
        1);

    return shadowPayload.shadow;
}

vec3 directLighting(vec3 hitPos) {
    vec3 final = vec3(0.0f);

    for (uint i = 0; i < lightCount; i++) {
        LightData light = lssbo[frame].lights[i];
        vec3 lightPos = light.pos.xyz;
        vec3 fragLightDir = normalize(lightPos - hitPos);

        vec3 Le = spotlightEmittedRadience(light, hitPos, lightPos, fragLightDir);
        if (length(Le) < 0.05f) continue;

        if (isShadowed(hitPos, lightPos, fragLightDir)) continue;

        final += Le;
    }

    return final;
}

void main() {
    // load the vertex data
    uint index = 3 * gl_PrimitiveID;
    vec2 uv;
    vec3 norm;
    vec3 tangent;
    getVertData(index, uv, norm, tangent);

    // load the textures
    vec4 albedo;
    vec4 metallicRoughness;
    vec3 normal;
    vec3 emissive;
    float occlusion;

    mat3 tbn = getTBN(tangent, mat3(gl_ObjectToWorldEXT), norm);
    getTextures(texIndices[gl_InstanceCustomIndexEXT], uv, tbn, albedo, metallicRoughness, normal, emissive, occlusion);

    vec3 rayPos = gl_WorldRayOriginEXT + (gl_WorldRayDirectionEXT * gl_HitTEXT);
    vec3 rayDir = gl_WorldRayDirectionEXT;

    payload.col = directLighting(rayPos);
    payload.terminate = true;
}
