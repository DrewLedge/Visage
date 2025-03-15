struct PrimaryPayload {
    vec3 col;
    uint rec;

    vec3 pos;
    vec3 dir;
    bool terminate;
};

struct ShadowPayload {
    float factor;
};
