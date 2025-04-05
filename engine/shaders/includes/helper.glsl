#define PI 3.14159265358979f

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

vec3 getNonParalellTangent(vec3 normal) {
    // find a new tangent to use that wont be parallel to the normal
    vec3 nonParallel = vec3(1.0f, 0.0f, 0.0f);

    // check if the new tangent is also parallel
    if (abs(dot(nonParallel, normal)) > 0.95f) {
        nonParallel = vec3(0.0f, 1.0f, 0.0f);
    }

    vec3 T = normalize(cross(normal, nonParallel));
    return T;
}

mat3 getTBN(vec3 tangent, mat3 model, vec3 normal) {
    mat3 normMat = transpose(inverse(model));
    vec3 N = normalize(normMat * normal);

    vec3 T = normalize(normMat * tangent);

    vec3 orthogonal = T - dot(T, N) * N;  // re orthogonalize tangent

    // if the tangent is parallel to the normal
    if (length(orthogonal) < 0.001f) {
        T = getNonParalellTangent(normal);
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

// calc the geometry function for a given term using Schlick-GGX approximation
// the geometry function accounts for the fact that some microfacets may be shadowed by others, which reduces the reflectance
// without the geometry function, rough surfaces would appear overly shiny
float gSchlickGGX(float term, float roughness) {
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    return term / (term * (1.0f - k) + k);
}

// calc the geometry function based on the light and view dir
// this determines which microfacets are shadowed, and thus cannot reflect light into the view dir
float gSmith(float NdotV, float NdotL, float roughness) {
    return gSchlickGGX(NdotV, roughness) * gSchlickGGX(NdotL, roughness);
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

vec3 evalCookTorrance(vec3 N, vec3 L, vec3 V, vec3 albedo, float metallic, float roughness) {
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
    vec3 F = fresnelTerm(albedo, metallic, VdotH);

    float norm = 4.0f * NdotV * NdotL;
    norm = max(norm, 0.001f);  // prevent divide by 0

    vec3 spec = (ND * G * F) / norm;

    // the proportion of light not reflected specularly
    vec3 kD = vec3(1.0f) - F;
    kD *= 1.0f - metallic;

    vec3 diffuse = kD * albedo / PI;

    return (diffuse + spec) * NdotL;
}

// "Sampling the GGX Distribution of Visible Normals" by Eric Heitz
// Check out: https://jcgt.org/published/0007/04/01/paper.pdf
vec3 sampleGGXVNDF(vec3 Ve, float alphaX, float alphay, float U1, float U2) {
    // Section 3.2: transforming the view direction to the hemisphere configuration
    vec3 Vh = normalize(vec3(alphaX * Ve.x, alphay * Ve.y, Ve.z));

    // Section 4.1: orthonormal basis (with special case if cross product is zero)
    float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
    vec3 T1 = lensq > 0 ? vec3(-Vh.y, Vh.x, 0) * inversesqrt(lensq) : vec3(1, 0, 0);
    vec3 T2 = cross(Vh, T1);

    // Section 4.2: parameterization of the projected area
    float r = sqrt(U1);
    float phi = 2.0 * PI * U2;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - t1 * t1) + s * t2;

    // Section 4.3: reprojection onto hemisphere
    vec3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

    // Section 3.4: transforming the normal back to the ellipsoid configuration
    vec3 Ne = normalize(vec3(alphaX * Nh.x, alphay * Nh.y, max(0.0, Nh.z)));
    return Ne;
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