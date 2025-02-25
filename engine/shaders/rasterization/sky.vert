#version 460

#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) out vec4 outFragPos;

layout(push_constant, std430) uniform PC {
    int frame;
};

layout(set = 1, binding = 0) uniform CamBufferObject {
    mat4 view;
    mat4 proj;
    mat4 iview;
    mat4 iproj;
}
CamUBO[];

const int indices[36] = {
    0, 1, 2, 2, 3, 0,
    4, 5, 1, 1, 0, 4,
    4, 0, 3, 3, 7, 4,
    7, 6, 5, 5, 4, 7,
    3, 2, 6, 6, 7, 3,
    1, 5, 6, 6, 2, 1};

const vec3 positions[8] = {
    vec3(-1.0f, 1.0f, 1.0f),
    vec3(-1.0f, -1.0f, 1.0f),
    vec3(1.0f, -1.0f, 1.0f),
    vec3(1.0f, 1.0f, 1.0f),
    vec3(-1.0f, 1.0f, -1.0f),
    vec3(-1.0f, -1.0f, -1.0f),
    vec3(1.0f, -1.0f, -1.0f),
    vec3(1.0f, 1.0f, -1.0f)};

void main() {
    mat4 view = CamUBO[frame].view;
    view[3] = vec4(0.0f, 0.0f, 0.0f, 1.0f);

    mat4 projection = CamUBO[frame].proj;

    vec3 pos = positions[indices[gl_VertexIndex]];
    outFragPos = vec4(pos, 1.0f);
    gl_Position = projection * view * outFragPos;
}
