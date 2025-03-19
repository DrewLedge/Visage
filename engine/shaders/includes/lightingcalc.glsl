vec3 spotlightEmittedRadience(LightData light, vec3 pos, vec3 lightPos, vec3 fragLightDir) {
    vec3 lightColor = light.color.xyz;
    vec3 target = light.target.xyz;

    float intensity = light.intensity;

    float inner = light.innerConeAngle;
    float outer = light.outerConeAngle;

    float constAttenuation = light.constantAttenuation;
    float linAttenuation = light.linearAttenuation;
    float quadAttenuation = light.quadraticAttenuation;

    // light falloff
    vec3 spotDir = normalize(target - lightPos);
    float theta = dot(spotDir, -fragLightDir);

    if (theta < cos(outer)) {
        return vec3(0.0f);
    }

    float falloff = smoothstep(cos(outer), cos(inner), theta);
    falloff *= falloff;

    // light attenuation
    float lightDistance = distance(lightPos, pos);
    float attenuation = 1.0f / (constAttenuation + linAttenuation * lightDistance + quadAttenuation * (lightDistance * lightDistance));

    return lightColor * intensity * falloff * attenuation;
}

float getShadowFactor(LightData light, int lightIndex, int frame, vec3 fragPos, int frameCount, int lightsPerBatch) {
    // get the frag pos in light space
    vec4 fragPosLightspace = light.vp * vec4(fragPos, 1.0f);

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
        LightData light = lssbo[frame].lights[i];

        if (light.intensity < 0.01f) continue;

        vec3 lightPos = light.pos.xyz;
        vec3 fragLightDir = normalize(lightPos - fragPos);

        vec3 Le = spotlightEmittedRadience(light, fragPos, lightPos, fragLightDir);
        if (length(Le) < 0.05f) continue;

        float shadowFactor = getShadowFactor(light, i, frame, fragPos, frameCount, lightsPerBatch);
        if (shadowFactor < 0.05f) continue;

        vec3 brdf = cookTorrance(normal, fragLightDir, viewDir, albedo, metallic, roughness);
        accumulated += (brdf * Le * shadowFactor);
    }

    // final color calculation
    vec3 o = albedo.rgb * occlusion * 0.005f;
    return vec4(accumulated + emissive + o, albedo.a);
}
