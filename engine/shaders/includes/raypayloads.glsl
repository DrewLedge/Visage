struct RayData {
    vec3 pos;
    vec3 dir;
    bool terminate;
};

struct PrimaryPayload {
    RayData ray;

    uint seed;

    vec3 col;
    vec3 throughput;

    uint rec;
};

struct ShadowPayload {
    bool shadow;
};
