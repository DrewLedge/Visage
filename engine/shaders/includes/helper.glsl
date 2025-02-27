#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

#define PI 3.14159265358979f

struct TexIndices {
    int albedo;
    int metallicRoughness;
    int normal;
    int emissive;
    int occlusion;

    uint64_t vertexAddress;
    uint64_t indexAddress;
};

struct LightData {
    vec4 pos;
    vec4 color;
    vec4 target;

    mat4 vp;

    float intensity;
    float innerConeAngle;
    float outerConeAngle;
    float constantAttenuation;
    float linearAttenuation;
    float quadraticAttenuation;
};

#ifdef RAYTRACING

#define MAX_RAY_RECURSION 3

struct PrimaryPayload {
    vec3 col;
    uint rec;
};

struct ShadowPayload {
    vec3 col;
    float factor;
};

struct Vertex {
    vec3 pos;
    vec2 tex;
    vec3 normal;
    vec3 tangent;
};

// barycentric interpolation for vec2 and vec3
#define BARYCENTRIC(type)                                                 \
    type barycentric##type(type b1, type b2, type b3, float u, float v) { \
        float w = 1.0f - u - v;                                           \
        return (b1 * w) + (b2 * u) + (b3 * v);                            \
    }

BARYCENTRIC(vec2)
BARYCENTRIC(vec3)

#endif

#ifdef VERT_SHADER
vec4 getPos(mat4 proj, mat4 view, mat4 model, vec3 pos) {
    return proj * view * model * vec4(pos, 1.0f);
}

vec4 getPos(mat4 viewProj, mat4 model, vec3 pos) {
    return viewProj * model * vec4(pos, 1.0f);
}

vec3 getViewDir(mat4 iview, mat4 model, vec3 pos) {
    vec3 worldCamPos = vec3(iview[3]);
    vec3 fragPos = vec3(model * vec4(pos, 1.0f));
    return normalize(worldCamPos - fragPos);
}

mat3 getTBN(vec3 tangent, mat3 model, vec3 normal) {
    mat3 normMat = transpose(inverse(model));
    vec3 N = normalize(normMat * normal);

    vec3 T = normalize(normMat * tangent);

    vec3 orthogonal = T - dot(T, N) * N;  // re orthogonalize tangent

    // if the tangent is parallel to the normal
    if (length(orthogonal) < 0.00001) {
        // find a new tangent to use that wont be parallel to the normal
        vec3 nonparallelTangent;

        // the closer c is to 1, the more parallel the tangent and normal are
        float c = abs(dot(vec3(1.0, 0.0, 0.0), N));

        // the vector (1.0f, 0.0f, 0.0f) is also parallel to the normal
        if (c > 0.95) {
            nonparallelTangent = vec3(0.0f, 1.0f, 0.0f);
        }

        // the vector (1.0f, 0.0f, 0.0f) isnt parallel to the normal so its good to use
        else {
            nonparallelTangent = vec3(1.0f, 0.0f, 0.0f);
        }

        // use the new non parallel tangent
        T = normalize(cross(N, nonparallelTangent));
    } else {
        T = normalize(orthogonal);
    }

    vec3 B = normalize(cross(N, T));
    return mat3(T, B, N);
}

mat3 getTBN(vec3 tangent, mat4 model, vec3 normal) {
    return getTBN(tangent, mat3(model), normal);
}

float getNearPlane(mat4 proj) {
    return -proj[3][2] / (proj[2][2] + 1.0f);
}

float getFarPlane(mat4 proj) {
    return -proj[3][2] / (proj[2][2] - 1.0f);
}

#endif

#ifdef FRAG_SHADER

// calc the geometry function for a given term using Schlick-GGX approximation
// the geometry function accounts for the fact that some microfacets may be shadowed by others, which reduces the reflectance
// without the geometry function, rough surfaces would appear overly shiny
float gSchlickGGX(float term, float k) {
    return term / (term * (1.0f - k) + k);
}

// calc the geometry function based on the light and view dir
// this determines which microfacets are shadowed, and thus cannot reflect light into the view dir
float gSmith(float NdotV, float NdotL, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    return gSchlickGGX(NdotV, k) * gSchlickGGX(NdotL, k);
}

// calc the normal distribution function (ndf) using Trowbridge-Reitz model
// the ndf determines the statistical distribution of microfacet orientations that contribute to the reflection
// ndf is crucial for determining the intensity and shape of specular highlights
float ndf(float NdotH, float a) {
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}

// calc the fresnel term using Schlick approximation
// the fresnel term determines how reflective the material is based on the viewing angle and metallic value
vec3 fresnelTerm(vec3 color, float metallic, float VdotH) {
    const vec3 br = vec3(0.04f);         // the base reflectivity constant for non metallic (dielectric) materials
    vec3 F0 = mix(br, color, metallic);  // the base reflectivity of the material based on the metallic value and the albedo
    return F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);
}

vec3 cookTorrance(vec3 N, vec3 L, vec3 V, vec4 albedo, float metallic, float roughness) {
    float a = roughness * roughness;

    // compute halfway vector
    vec3 H = normalize(V + L);

    // compute the dot products
    float NdotH = max(dot(N, H), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);
    float VdotH = max(dot(V, H), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);

    // normal distribution function
    float ND = ndf(NdotH, a);

    // geometry function
    float G = gSmith(NdotV, NdotL, roughness);

    // fresnel term
    vec3 F = fresnelTerm(albedo.rgb, metallic, VdotH);

    float norm = (4.0f * max(NdotV * NdotL, 0.0001f));  // used to normalize the specular term
    vec3 spec = (ND * G * F) / norm;

    // the proportion of light not reflected specularly
    vec3 kD = vec3(1.0f) - F;
    kD *= 1.0f - metallic;

    vec3 diffuse = kD * albedo.rgb / PI;

    return (diffuse + spec) * NdotL;
}

float linDepth(float depth, float near, float far) {
    float n = far * near;
    float d = far + depth * (near - far);

    return n / d;
}

vec2 getTexCoords(sampler2D tex, vec2 fragCoord) {
    ivec2 texDimensions = textureSize(tex, 0);
    return fragCoord / texDimensions;
}

float calcFallofff(float outer, float inner, float theta) {
    float f = smoothstep(cos(outer), cos(inner), theta);
    return f * f;
}

vec3 getFragPos(vec2 uv, float depth, mat4 iproj, mat4 iview) {
    // convert uv to -1 to 1 range
    vec2 ndc = uv * 2.0f - 1.0f;

    // get the clip space pos
    vec4 clip = vec4(ndc, depth, 1.0f);

    // multiply by the inverse proj mat to get the pos in view space
    vec4 view = iproj * clip;
    view /= view.w;  // perspective divide

    // multiply by the inverse view mat to get the pos in world space
    return (iview * view).xyz;
}

vec3 getViewDir(vec3 fragWorldPos, mat4 iview) {
    vec3 camPos = vec3(iview[3]);
    return normalize(camPos - fragWorldPos);
}

void getTextures(TexIndices texIndices, vec2 uv, mat3 tbn, out vec4 albedo, out vec4 metallicRoughness, out vec3 normal, out vec3 emissive, out float occlusion) {
    bool albedoExists = (texIndices.albedo >= 0);
    bool metallicRoughnessExists = (texIndices.metallicRoughness >= 0);
    bool normalExists = (texIndices.normal >= 0);
    bool emissiveExists = (texIndices.emissive >= 0);
    bool occlusionExists = (texIndices.occlusion >= 0);

    // default values
    albedo = vec4(1.0f, 0.0f, 0.0f, 1.0f);  // solid red if missing
    metallicRoughness = vec4(0.0f, 0.5f, 0.0f, 1.0f);
    normal = vec3(0.0f);
    emissive = vec3(0.0f);
    occlusion = 1.0f;

    if (albedoExists) {
        albedo = texture(texSamplers[texIndices.albedo], uv);
    }

    if (metallicRoughnessExists) {
        metallicRoughness = texture(texSamplers[texIndices.metallicRoughness], uv);
    }

    if (normalExists) {
        normal = (texture(texSamplers[texIndices.normal], uv).rgb * 2.0f - 1.0f);
        normal = normalize(tbn * normal);
    }

    if (emissiveExists) {
        emissive = texture(texSamplers[texIndices.emissive], uv).rgb;
    }

    if (occlusionExists) {
        occlusion = texture(texSamplers[texIndices.occlusion], uv).r * metallicRoughness.b;
    }
}

#endif
