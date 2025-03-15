float getAttenuation(int lightIndex, int frame, vec3 lightPos, vec3 fragPos) {
    float constAttenuation = lssbo[frame].lights[lightIndex].constantAttenuation;
    float linAttenuation = lssbo[frame].lights[lightIndex].linearAttenuation;
    float quadAttenuation = lssbo[frame].lights[lightIndex].quadraticAttenuation;
    float lightDistance = distance(lightPos, fragPos);

    return 1.0f / (constAttenuation + linAttenuation * lightDistance + quadAttenuation * (lightDistance * lightDistance));
}

float getLightConeFactor(int lightIndex, int frame, vec3 fragLightDir, vec3 lightPos) {
    float inner = lssbo[frame].lights[lightIndex].innerConeAngle;
    float outer = lssbo[frame].lights[lightIndex].outerConeAngle;

    vec3 target = lssbo[frame].lights[lightIndex].target.xyz;
    vec3 spotDir = normalize(lightPos - target);

    float theta = dot(spotDir, fragLightDir);

    if (theta <= cos(outer)) {
        return 0.0f;
    }

    float factor = smoothstep(cos(outer), cos(inner), theta);
    return factor * factor;
}

float getShadowFactor(int lightIndex, int frame, vec3 fragPos, int frameCount, int lightsPerBatch) {
    // get the frag pos in light space
    vec4 fragPosLightspace = lssbo[frame].lights[lightIndex].vp * vec4(fragPos, 1.0f);

    // perspective divide and transfrom x and y compoenent to 0 - 1 range
    // this is because the z component is already in the 0 - 1 range
    vec3 projCoords = fragPosLightspace.xyz / fragPosLightspace.w;
    projCoords.xy = projCoords.xy * 0.5f + 0.5f;

    // get the texture index inside the batch
    float texIndex = float(lightIndex % lightsPerBatch);
    vec4 shadowCoords = vec4(projCoords.xy, texIndex, projCoords.z);

    // get the texture index
    int batchIndex = int(lightIndex / lightsPerBatch);
    int shadowTexIndex = (batchIndex * frameCount) + frame;

    // get the shadow factor
    return texture(shadowMapSamplers[shadowTexIndex], shadowCoords);
}

vec4 calcLighting(vec4 albedo, vec4 metallicRoughness, vec3 normal, vec3 emissive, float occlusion, vec3 fragPos, vec3 viewDir, int frame, int frameCount, int lightCount, int lightsPerBatch) {
    vec3 accumulated = vec3(0.0f);

    float roughness = metallicRoughness.g;
    float metallic = metallicRoughness.b;

    for (int i = 0; i < lightCount; i++) {
        if (lssbo[frame].lights[i].intensity < 0.01f) continue;

        vec3 lightPos = lssbo[frame].lights[i].pos.xyz;
        vec3 lightColor = lssbo[frame].lights[i].color.xyz;
        vec3 fragLightDir = normalize(lightPos - fragPos);

        float lightConeFactor = getLightConeFactor(i, frame, fragLightDir, lightPos);
        if (lightConeFactor < 0.01f) continue;

        float shadowFactor = getShadowFactor(i, frame, fragPos, frameCount, lightsPerBatch);
        if (shadowFactor < 0.04f) continue;

        float attenuation = getAttenuation(i, frame, lightPos, fragPos);
        if (attenuation < 0.01f) continue;

        float contribution = lssbo[frame].lights[i].intensity * attenuation * lightConeFactor;
        if (contribution < 0.01f) continue;

        vec3 brdf = cookTorrance(normal, fragLightDir, viewDir, albedo, metallic, roughness);
        accumulated += (brdf * lightColor * contribution * shadowFactor);
    }

    // final color calculation
    vec3 o = albedo.rgb * occlusion * 0.005f;
    return vec4(accumulated + emissive + o, albedo.a);
}
