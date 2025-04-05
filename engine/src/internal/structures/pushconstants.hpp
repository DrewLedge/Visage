#pragma once

namespace pushconstants {
struct FramePushConst {
    int frame;
};

struct LightPushConst {
    int lightCount;
    int frameCount;
    int lightsPerBatch;
};

struct RTPushConst {
    int frame;
    int lightCount;
    uint32_t frameCount;
};

struct ShadowPushConst {
    int frame;
    int batch;
    int lightCount;
    int lightsPerBatch;
};

struct ObjectPushConst {
    int bitfield;  // bitfield of which textures exist
    int start;     // starting index of the textures in the texture array
};
}  // namespace pushconstants
