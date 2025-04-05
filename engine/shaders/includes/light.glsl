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
