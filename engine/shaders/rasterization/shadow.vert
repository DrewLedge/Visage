#version 460

#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_multiview : require

#define VERT_SHADER

#include "../includes/helper.glsl"

layout(location = 0) in vec3 inPosition;

// individual rows of the instanced model matrix
layout(location = 1) in vec4 inModel1;
layout(location = 2) in vec4 inModel2;
layout(location = 3) in vec4 inModel3;
layout(location = 4) in vec4 inModel4;

layout(location = 0) out uint outDiscard;

layout(set = 0, binding = 0) readonly buffer LightBuffer {
    LightData lights[];
}
lssbo[];

layout(push_constant, std430) uniform pc {
    int frame;
    int batch;
    int lightCount;
    int lightsPerBatch;
};

void main() {
    int lightIndex = (batch * lightsPerBatch) + gl_ViewIndex;

    if (lightIndex >= lightCount) {
        gl_Position = vec4(0.0f);
        outDiscard = 1;
        return;
    }

    mat4 model = mat4(inModel1, inModel2, inModel3, inModel4);
    gl_Position = getPos(lssbo[frame].lights[lightIndex].vp, model, inPosition);

    outDiscard = 0;
}
