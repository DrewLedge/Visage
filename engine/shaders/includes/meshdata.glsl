struct Vertex {
    vec3 pos;
    vec2 tex;
    vec3 normal;
    vec3 tangent;
};

layout(buffer_reference) readonly buffer VertBuffer {
    Vertex vertices[];
};

layout(buffer_reference) readonly buffer IndexBuffer {
    uint indices[];
};

// barycentric interpolation for vec2 and vec3
#define BARYCENTRIC(type)                                                 \
    type barycentric##type(type b1, type b2, type b3, float u, float v) { \
        float w = 1.0f - u - v;                                           \
        return (b1 * w) + (b2 * u) + (b3 * v);                            \
    }

BARYCENTRIC(vec2)
BARYCENTRIC(vec3)
