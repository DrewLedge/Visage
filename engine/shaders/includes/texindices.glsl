#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct TexIndices {
    int albedo;
    int metallicRoughness;
    int normal;
    int emissive;
    int occlusion;

    uint64_t vertexAddress;
    uint64_t indexAddress;
};
