#version 460

#extension GL_EXT_ray_tracing : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

#define RAYTRACING
#define FRAG_SHADER
#define VERT_SHADER

layout(set = 0, binding = 0) uniform sampler2D texSamplers[];

#include "../includes/helper.glsl"

layout(push_constant, std430) uniform pc {
    layout(offset = 4) int frame;
    int lightCount;
};

layout(set = 1, binding = 0) readonly buffer LightBuffer {
    LightData lights[];
}
lssbo[];

layout(set = 5, binding = 0) uniform accelerationStructureEXT TLAS[];

layout(set = 6, binding = 0) readonly buffer TexIndexBuffer {
    TexIndices texIndices[];
};

layout(buffer_reference) readonly buffer VertBuffer {
    Vertex vertices[];
};

layout(buffer_reference) readonly buffer IndexBuffer {
    uint indices[];
};

layout(location = 0) rayPayloadInEXT PrimaryPayload payload;
layout(location = 1) rayPayloadEXT ShadowPayload shadowPayload;
hitAttributeEXT vec2 hit;

#include "../includes/lightingcalc.glsl"

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

void main() {
    if (payload.rec >= MAX_RAY_RECURSION) {
        payload.col = vec3(0);
        return;
    }

    uint index = 3 * gl_PrimitiveID;

    // load the vertex data
    vec2 uv;
    vec3 norm;
    vec3 tangent;

    getVertData(index, uv, norm, tangent);

    // load the texture data
    vec4 albedo;
    vec4 metallicRoughness;
    vec3 normal;
    vec3 emissive;
    float occlusion;

    mat3 tbn = getTBN(tangent, mat3(gl_ObjectToWorldEXT), norm);

    getTextures(texIndices[gl_InstanceCustomIndexEXT], uv, tbn, albedo, metallicRoughness, normal, emissive, occlusion);

    vec3 hitPos = gl_WorldRayOriginEXT + (gl_WorldRayDirectionEXT * gl_HitTEXT);
    vec3 viewDir = -gl_WorldRayDirectionEXT;
    vec3 reflectDir = reflect(gl_WorldRayDirectionEXT, normal);

    payload.rec++;

    // trace reflection rays
    traceRayEXT(
        TLAS[frame],
        0,           // flags
        0xFF,        // cull mask
        0,           // sbt offset
        0,           // sbt stride
        0,           // miss index
        hitPos,      // pos
        0.01f,       // min-range
        reflectDir,  // dir
        100.0f,      // max-range
        0            // payload
    );

    payload.rec--;

    float roughness = metallicRoughness.g;
    float metallic = metallicRoughness.b;

    // fresnel term
    vec3 H = normalize(viewDir + reflectDir);
    float VdotH = max(dot(viewDir, H), 0.0f);
    vec3 F = fresnelTerm(albedo.rgb, metallic, VdotH);

    // get the reflection color
    vec3 refl = payload.col * F * (1.0f - roughness);

    const float minShadowRayDist = 0.01f;

    // set the payload color
    vec3 direct = calcLighting(albedo, metallicRoughness, normal, emissive, occlusion, hitPos, viewDir, frame, lightCount, minShadowRayDist);
    payload.col = direct + refl;
}
