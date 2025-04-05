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
