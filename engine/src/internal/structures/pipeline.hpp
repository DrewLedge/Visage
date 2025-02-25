#pragma once

#include "../../libraries/vkhelper.hpp"

namespace pipeline {
struct PipelineData {
    VkhRenderPass renderPass{};
    VkhPipelineLayout layout{};
    VkhPipeline pipeline{};

    void reset() {
        renderPass.reset();
        layout.reset();
        pipeline.reset();
    }
};
}  // namespace pipeline
