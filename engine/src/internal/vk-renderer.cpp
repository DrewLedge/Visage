#include "vk-renderer.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include "config.hpp"

namespace renderer {
void VkRenderer::init(bool rtEnabled, bool showDebugInfo, VkDevice device, const setup::VkSetup* setup, const swapchain::VkSwapChain* swap, const textures::VkTextures* textures, const scene::VkScene* scene, const buffers::VkBuffers* buffers, const descriptorsets::VkDescriptorSets* descs, const pipelines::VkPipelines* pipelines, const raytracing::VkRaytracing* raytracing) noexcept {
    m_setup = setup;
    m_swap = swap;
    m_textures = textures;
    m_scene = scene;
    m_buffers = buffers;
    m_descs = descs;
    m_pipe = pipelines;
    m_raytracing = raytracing;

    m_rtEnabled = rtEnabled;
    m_showDebugInfo = showDebugInfo;
    m_device = device;

    m_commandPool = vkh::createCommandPool(m_setup->getGraphicsFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    setupFences();
    createSemaphores();
}

void VkRenderer::VkRenderer::createCommandBuffers() {
    m_commandPool = vkh::createCommandPool(m_setup->getGraphicsFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    uint32_t maxFrames = m_swap->getMaxFrames();

    if (!m_rtEnabled) {
        allocateCommandBuffers(m_deferredCB, maxFrames, 1);
        allocateCommandBuffers(m_shadowCB, m_scene->getShadowBatchCount() * maxFrames, 0);
        allocateCommandBuffers(m_lightingCB, maxFrames, 0);
        allocateCommandBuffers(m_wboitCB, maxFrames, 0);
    } else {
        allocateCommandBuffers(m_rtCB, maxFrames, 0);
    }

    allocateCommandBuffers(m_compCB, m_swap->getImageCount(), 0);

    m_frameShadowCommandBuffers.reserve(m_scene->getLightCount());
}

void VkRenderer::VkRenderer::createFrameBuffers(bool shadow) {
    if (!m_rtEnabled) {
        m_deferredFB.resize(m_swap->getMaxFrames());
        m_shadowFB.reserve(m_swap->getMaxFrames() * m_scene->getShadowBatchCount());
        m_lightingFB.resize(m_swap->getMaxFrames());
        m_wboitFB.resize(m_swap->getMaxFrames());

        // shadowmap framebuffers
        if (shadow) {
            for (size_t j = 0; j < m_scene->getShadowBatchCount(); j++) {
                for (size_t i = 0; i < m_swap->getMaxFrames(); i++) {
                    VkhFramebuffer fb{};
                    vkh::createFB(m_pipe->getShadowPipe().renderPass, fb, m_textures->getShadowTex(j, i).imageView.p(), 1, cfg::SHADOW_WIDTH, cfg::SHADOW_HEIGHT);
                    m_shadowFB.push_back(fb);
                }
            }
        }

        for (size_t i = 0; i < m_swap->getMaxFrames(); i++) {
            // deferred pass framebuffers
            std::array<VkImageView, 5> attachments{};
            for (size_t j = 0; j < 4; j++) {
                size_t k = (i * 4) + j;

                const vkh::Texture& colorT = m_textures->getDeferredColorTex(k);
                attachments[j] = colorT.imageView.v();
            }

            const vkh::Texture& depthT = m_textures->getDeferredDepthTex(i);
            attachments[4] = depthT.imageView.v();
            vkh::createFB(m_pipe->getDeferredPipe().renderPass, m_deferredFB[i], attachments.data(), 5, m_swap->getWidth(), m_swap->getHeight());

            // lighting pass framebuffer
            const vkh::Texture& lightingT = m_textures->getLightingTex(i);
            vkh::createFB(m_pipe->getLightingPipe().renderPass, m_lightingFB[i], lightingT.imageView.p(), 1, m_swap->getWidth(), m_swap->getHeight());

            // wboit framebuffer
            const vkh::Texture& wboitT = m_textures->getWboitTex(i);
            vkh::createFB(m_pipe->getWBOITPipe().renderPass, m_wboitFB[i], wboitT.imageView.p(), 1, m_swap->getWidth(), m_swap->getHeight());
        }
    }

    // swap frame buffers
    VkhRenderPass compRenderpass = m_pipe->getCompPipe().renderPass;
    uint32_t imageCount = m_swap->getImageCount();
    m_swapFB.resize(imageCount);

    if (m_textures->getCompTexCount() != imageCount) {
        throw std::runtime_error("Texture size doesn't match swap image count!");
    }

    const vkh::Texture* compTextures = m_textures->getCompTextures();
    for (size_t i = 0; i < imageCount; i++) {
        std::array<VkImageView, 2> attachments = {compTextures[i].imageView.v(), m_swap->getImageView(i)};
        vkh::createFB(compRenderpass, m_swapFB[i], attachments.data(), 2, m_swap->getWidth(), m_swap->getHeight());
    }
}

VkResult VkRenderer::drawFrame(uint32_t currentFrame, float fps) {
    m_currentFrame = currentFrame;
    m_fps = fps;

    recordAllCommandBuffers();

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    std::vector<VkSubmitInfo> submitInfos;

    if (!m_rtEnabled) {
        submitInfos.push_back(vkh::createSubmitInfo(m_deferredCB.primary[m_currentFrame].p(), 1, &waitStage, m_imageAvailableSemaphores[m_currentFrame], m_deferredSemaphores[m_currentFrame]));
        submitInfos.push_back(vkh::createSubmitInfo(m_frameShadowCommandBuffers.data(), m_frameShadowCommandBuffers.size(), &waitStage, m_deferredSemaphores[m_currentFrame], m_shadowSemaphores[m_currentFrame]));
        submitInfos.push_back(vkh::createSubmitInfo(m_lightingCB.primary[m_currentFrame].p(), 1, &waitStage, m_shadowSemaphores[m_currentFrame], m_wboitSemaphores[m_currentFrame]));
        submitInfos.push_back(vkh::createSubmitInfo(m_wboitCB.primary[m_currentFrame].p(), 1, &waitStage, m_wboitSemaphores[m_currentFrame], m_compSemaphores[m_currentFrame]));
        submitInfos.push_back(vkh::createSubmitInfo(m_compCB.primary[m_currentFrame].p(), 1, &waitStage, m_compSemaphores[m_currentFrame], m_renderFinishedSemaphores[m_currentFrame]));
    } else {
        submitInfos.push_back(vkh::createSubmitInfo(m_rtCB.primary[m_currentFrame].p(), 1, &waitStage, m_imageAvailableSemaphores[m_currentFrame], m_rtSemaphores[m_currentFrame]));
        submitInfos.push_back(vkh::createSubmitInfo(m_compCB.primary[m_currentFrame].p(), 1, &waitStage, m_rtSemaphores[m_currentFrame], m_renderFinishedSemaphores[m_currentFrame]));
    }

    // submit all command buffers in a single call
    if (vkQueueSubmit(m_setup->gQueue(), static_cast<uint32_t>(submitInfos.size()), submitInfos.data(), m_fences[m_currentFrame].v()) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit command buffers!");
    }

    // present the image
    uint32_t imageIndex = m_swap->getImageIndex();
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = m_renderFinishedSemaphores[m_currentFrame].p();
    presentInfo.pSwapchains = m_swap->getSwapP();
    presentInfo.swapchainCount = 1;
    presentInfo.pImageIndices = &imageIndex;

    return vkQueuePresentKHR(m_setup->pQueue(), &presentInfo);
}

void VkRenderer::reallocateLights() {
    allocateCommandBuffers(m_shadowCB, m_scene->getShadowBatchCount() * m_swap->getMaxFrames(), 0);
    m_shadowFB.clear();
    m_shadowFB.reserve(m_swap->getMaxFrames());
    m_frameShadowCommandBuffers.clear();
}

void VkRenderer::addShadowFrameBuffer(const vkh::Texture& tex) {
    VkhFramebuffer fb{};
    vkh::createFB(m_pipe->getShadowPipe().renderPass, fb, tex.imageView.p(), 1, cfg::SHADOW_WIDTH, cfg::SHADOW_HEIGHT);
    m_shadowFB.push_back(fb);
}

void VkRenderer::addShadowCommandBuffers() {
    uint32_t graphicsFamily = m_setup->getGraphicsFamily();

    VkhCommandPool p1 = vkh::createCommandPool(graphicsFamily, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VkhCommandBuffer c1 = vkh::allocateCommandBuffers(p1);
    m_shadowCB.primary.pools.push_back(p1);
    m_shadowCB.primary.buffers.push_back(c1);
}

void VkRenderer::setupFences() {
    m_fences.resize(m_swap->getMaxFrames());
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < m_fences.size(); i++) {
        if (vkCreateFence(m_device, &fenceInfo, nullptr, m_fences[i].p()) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void VkRenderer::createSemaphores() {
    for (size_t i = 0; i < m_swap->getMaxFrames(); i++) {
        m_imageAvailableSemaphores.push_back(vkh::createSemaphore());
        m_renderFinishedSemaphores.push_back(vkh::createSemaphore());

        if (!m_rtEnabled) {
            m_deferredSemaphores.push_back(vkh::createSemaphore());
            m_shadowSemaphores.push_back(vkh::createSemaphore());
            m_wboitSemaphores.push_back(vkh::createSemaphore());
        } else {
            m_rtSemaphores.push_back(vkh::createSemaphore());
        }

        m_compSemaphores.push_back(vkh::createSemaphore());
    }
}

void VkRenderer::VkRenderer::allocateCommandBuffers(commandbuffers::CommandBufferSet& cmdBuffers, size_t primaryCount, size_t secondaryCount) {
    cmdBuffers.primary.reserveClear(primaryCount);

    for (size_t i = 0; i < primaryCount; i++) {
        cmdBuffers.primary.pools.push_back(vkh::createCommandPool(m_setup->getGraphicsFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
        cmdBuffers.primary.buffers.push_back(vkh::allocateCommandBuffers(cmdBuffers.primary.pools[i]));
    }

    if (secondaryCount) {
        cmdBuffers.secondary.reserveClear(secondaryCount);

        for (size_t i = 0; i < secondaryCount; i++) {
            cmdBuffers.secondary.pools.push_back(vkh::createCommandPool(m_setup->getGraphicsFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT));
            cmdBuffers.secondary.buffers.push_back(vkh::allocateCommandBuffers(cmdBuffers.secondary.pools[i], VK_COMMAND_BUFFER_LEVEL_SECONDARY));
        }
    }
}

void VkRenderer::renderImguiFrame(VkhCommandBuffer& commandBuffer) {
    // begin new frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const float p = 10.0f;

    float x = static_cast<float>(m_swap->getWidth()) - p;
    float y = p;
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always, ImVec2(1.0f, 0.0f));

    // window flags
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings;

    // style settings
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

    // text to display
    std::vector<std::string> text;
    text.push_back("FPS: " + std::to_string(static_cast<int>(m_fps)));
    text.push_back("Objects: " + std::to_string(m_scene->getObjectCount()));
    text.push_back("Lights: " + std::to_string(m_scene->getLightCount()));
    text.push_back("Raytracing: " + std::string(m_rtEnabled ? "ON" : "OFF"));

    // render the frame
    if (ImGui::Begin("Info", nullptr, flags)) {
        for (const auto& t : text) {
            ImGui::TextUnformatted(t.c_str());
        }
    }

    ImGui::End();
    ImGui::PopStyleVar(1);

    // render frame
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.v());
}

void VkRenderer::recordObjectCommandBuffers(VkhCommandBuffer& secondary, const pipeline::PipelineData& pipe, const VkCommandBufferBeginInfo& beginInfo, const VkDescriptorSet* descriptorsets, size_t descriptorCount) {
    const std::array<VkBuffer, 2> vertexBuffersArray = {m_scene->getVertBuffer().buf.v(), m_buffers->getObjectInstanceBuffer(m_currentFrame).buf.v()};
    const std::array<VkDeviceSize, 2> offsets = {0, 0};

    vkCmdBindPipeline(secondary.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.pipeline.v());
    vkCmdBindDescriptorSets(secondary.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout.v(), 0, static_cast<uint32_t>(descriptorCount), descriptorsets, 0, nullptr);

    vkCmdBindVertexBuffers(secondary.v(), 0, 2, vertexBuffersArray.data(), offsets.data());
    vkCmdBindIndexBuffer(secondary.v(), m_scene->getIndexBuffer().buf.v(), 0, VK_INDEX_TYPE_UINT32);

    VkBuffer sceneIndirectBuffer = m_buffers->getSceneIndirectCommandsBuffer();
    vkCmdDrawIndexedIndirect(secondary.v(), sceneIndirectBuffer, 0, static_cast<uint32_t>(m_scene->getUniqueObjectCount()), sizeof(VkDrawIndexedIndirectCommand));
}

void VkRenderer::recordDeferredCommandBuffers() {
    const std::vector<VkDescriptorSet> sets = m_descs->getSets(descriptorsets::PASSES::DEFERRED);

    pipeline::PipelineData deferredPipe = m_pipe->getDeferredPipe();

    std::array<VkClearValue, 5> clearValues{};
    clearValues.fill(VkClearValue{{{0.0f, 0.0f, 0.0f, 1.0f}}});
    clearValues[4] = VkClearValue{{{1.0f, 0.0f}}};

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VkhCommandBuffer& deferredCommandBuffer = m_deferredCB.primary[m_currentFrame];
    if (vkBeginCommandBuffer(deferredCommandBuffer.v(), &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = deferredPipe.renderPass.v();
    renderPassInfo.framebuffer = m_deferredFB[m_currentFrame].v();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swap->getExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(deferredCommandBuffer.v(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdPushConstants(deferredCommandBuffer.v(), deferredPipe.layout.v(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushconstants::FramePushConst), &m_framePushConst);
    recordObjectCommandBuffers(deferredCommandBuffer, deferredPipe, beginInfo, sets.data(), sets.size());

    vkCmdEndRenderPass(deferredCommandBuffer.v());
    if (vkEndCommandBuffer(deferredCommandBuffer.v()) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VkRenderer::recordShadowCommandBuffers() {
    const std::vector<VkDescriptorSet> sets = m_descs->getSets(descriptorsets::PASSES::SHADOW);
    const std::array<VkBuffer, 2> vertexBuffersArray = {m_scene->getVertBuffer().buf.v(), m_buffers->getObjectInstanceBuffer(m_currentFrame).buf.v()};
    const std::array<VkDeviceSize, 2> offsets = {0, 0};

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    const VkClearValue clearValue = VkClearValue{{{1.0f, 0}}};
    pipeline::PipelineData shadowPipe = m_pipe->getShadowPipe();
    VkBuffer sceneIndirectBuffer = m_buffers->getSceneIndirectCommandsBuffer();

    size_t batchCount = m_scene->getShadowBatchCount();

    for (size_t i = 0; i < batchCount; i++) {
        size_t index = (i * m_swap->getMaxFrames()) + m_currentFrame;
        VkCommandBuffer& shadowCommandBuffer = m_shadowCB.primary.buffers[index].v();

        // begin command buffer
        if (vkBeginCommandBuffer(shadowCommandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("failed to begin recording command buffer!");
        }

        // begin render pass
        VkRenderPassBeginInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassInfo.renderPass = shadowPipe.renderPass.v();
        renderPassInfo.framebuffer = m_shadowFB[index].v();
        renderPassInfo.renderArea.offset = {0, 0};
        renderPassInfo.renderArea.extent = {cfg::SHADOW_WIDTH, cfg::SHADOW_HEIGHT};
        renderPassInfo.clearValueCount = 1;
        renderPassInfo.pClearValues = &clearValue;
        vkCmdBeginRenderPass(shadowCommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(shadowCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe.pipeline.v());
        vkCmdBindDescriptorSets(shadowCommandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipe.layout.v(), 0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

        pushconstants::ShadowPushConst shadowPushConst{};
        shadowPushConst.frame = m_currentFrame;
        shadowPushConst.batch = static_cast<int>(i);
        shadowPushConst.lightCount = static_cast<int>(m_scene->getLightCount());
        shadowPushConst.lightsPerBatch = cfg::LIGHTS_PER_BATCH;

        vkCmdPushConstants(shadowCommandBuffer, shadowPipe.layout.v(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushconstants::ShadowPushConst), &shadowPushConst);

        vkCmdBindVertexBuffers(shadowCommandBuffer, 0, 2, vertexBuffersArray.data(), offsets.data());
        vkCmdBindIndexBuffer(shadowCommandBuffer, m_scene->getIndexBuffer().buf.v(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexedIndirect(shadowCommandBuffer, sceneIndirectBuffer, 0, static_cast<uint32_t>(m_scene->getUniqueObjectCount()), sizeof(VkDrawIndexedIndirectCommand));

        // end the render pass and command buffer
        vkCmdEndRenderPass(shadowCommandBuffer);
        if (vkEndCommandBuffer(shadowCommandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to record command buffer!");
        }

        if (i >= m_frameShadowCommandBuffers.size()) {
            m_frameShadowCommandBuffers.push_back(shadowCommandBuffer);
        } else {
            m_frameShadowCommandBuffers[i] = shadowCommandBuffer;
        }
    }
}

void VkRenderer::recordLightingCommandBuffers() {
    const std::vector<VkDescriptorSet> lightingSets = m_descs->getSets(descriptorsets::PASSES::LIGHTING);
    const std::vector<VkDescriptorSet> skyboxSets = m_descs->getSets(descriptorsets::PASSES::SKYBOX);

    pipeline::PipelineData lightingPipe = m_pipe->getLightingPipe();
    pipeline::PipelineData skyboxPipe = m_pipe->getSkyboxPipe();

    const VkClearValue clearValue = VkClearValue{{{0.18f, 0.3f, 0.30f, 1.0f}}};

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VkhCommandBuffer& lightingCommandBuffer = m_lightingCB.primary[m_currentFrame];
    if (vkBeginCommandBuffer(lightingCommandBuffer.v(), &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = lightingPipe.renderPass.v();
    renderPassInfo.framebuffer = m_lightingFB[m_currentFrame].v();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swap->getExtent();
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearValue;

    const vkh::Texture& deferredDepth = m_textures->getDeferredDepthTex(m_currentFrame);
    vkh::transitionImageLayout(lightingCommandBuffer, deferredDepth.image, m_textures->getDepthFormat(), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, 1, 0);

    vkCmdBeginRenderPass(lightingCommandBuffer.v(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    // skybox
    vkCmdBindPipeline(lightingCommandBuffer.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipe.pipeline.v());
    vkCmdBindDescriptorSets(lightingCommandBuffer.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, skyboxPipe.layout.v(), 0, static_cast<uint32_t>(skyboxSets.size()), skyboxSets.data(), 0, nullptr);

    vkCmdPushConstants(lightingCommandBuffer.v(), skyboxPipe.layout.v(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushconstants::FramePushConst), &m_framePushConst);
    vkCmdDraw(lightingCommandBuffer.v(), 36, 1, 0, 0);

    // lighting
    vkCmdBindPipeline(lightingCommandBuffer.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipe.pipeline.v());
    vkCmdBindDescriptorSets(lightingCommandBuffer.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, lightingPipe.layout.v(), 0, static_cast<uint32_t>(lightingSets.size()), lightingSets.data(), 0, nullptr);

    vkCmdPushConstants(lightingCommandBuffer.v(), lightingPipe.layout.v(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushconstants::FramePushConst), &m_framePushConst);
    vkCmdPushConstants(lightingCommandBuffer.v(), lightingPipe.layout.v(), VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(pushconstants::FramePushConst), sizeof(pushconstants::LightPushConst), &m_lightPushConst);

    vkCmdDraw(lightingCommandBuffer.v(), 6, 1, 0, 0);
    vkCmdEndRenderPass(lightingCommandBuffer.v());

    if (vkEndCommandBuffer(lightingCommandBuffer.v()) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VkRenderer::recordWBOITCommandBuffers() {
    const std::vector<VkDescriptorSet> sets = m_descs->getSets(descriptorsets::PASSES::WBOIT);

    pipeline::PipelineData wboitPipe = m_pipe->getWBOITPipe();

    const std::array<VkClearValue, 2> clearValues = {VkClearValue{{{0.0f, 0.0f, 0.0f, 1.0f}}}, VkClearValue{{{1.0f, 0}}}};

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    VkhCommandBuffer& wboitCommandBuffer = m_wboitCB.primary[m_currentFrame];
    if (vkBeginCommandBuffer(wboitCommandBuffer.v(), &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = wboitPipe.renderPass.v();
    renderPassInfo.framebuffer = m_wboitFB[m_currentFrame].v();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swap->getExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(wboitCommandBuffer.v(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdPushConstants(wboitCommandBuffer.v(), wboitPipe.layout.v(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushconstants::FramePushConst), &m_framePushConst);
    vkCmdPushConstants(wboitCommandBuffer.v(), wboitPipe.layout.v(), VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(pushconstants::FramePushConst), sizeof(pushconstants::LightPushConst), &m_lightPushConst);

    recordObjectCommandBuffers(wboitCommandBuffer, wboitPipe, beginInfo, sets.data(), sets.size());

    vkCmdEndRenderPass(wboitCommandBuffer.v());

    if (vkEndCommandBuffer(wboitCommandBuffer.v()) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VkRenderer::recordCompCommandBuffers() {
    pipeline::PipelineData compPipe = m_pipe->getCompPipe();

    const std::array<VkClearValue, 2> clearValues = {VkClearValue{{{0.18f, 0.3f, 0.30f, 1.0f}}}, VkClearValue{{{1.0f, 0}}}};

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    beginInfo.pInheritanceInfo = nullptr;

    const std::vector<VkDescriptorSet> sets = m_descs->getSets(descriptorsets::PASSES::COMP);
    const VkDescriptorSet* set = (m_rtEnabled) ? &sets[0] : &sets[1];

    VkhCommandBuffer& compCommandBuffer = m_compCB.primary.buffers[m_currentFrame];
    if (vkBeginCommandBuffer(compCommandBuffer.v(), &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = compPipe.renderPass.v();
    renderPassInfo.framebuffer = m_swapFB[m_swap->getImageIndex()].v();
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = m_swap->getExtent();
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(compCommandBuffer.v(), &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(compCommandBuffer.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, compPipe.pipeline.v());
    vkCmdBindDescriptorSets(compCommandBuffer.v(), VK_PIPELINE_BIND_POINT_GRAPHICS, compPipe.layout.v(), 0, 1, set, 0, nullptr);

    vkCmdPushConstants(compCommandBuffer.v(), compPipe.layout.v(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushconstants::FramePushConst), &m_framePushConst);

    vkCmdDraw(compCommandBuffer.v(), 6, 1, 0, 0);

    if (m_showDebugInfo) {
        renderImguiFrame(compCommandBuffer);
    }

    vkCmdEndRenderPass(compCommandBuffer.v());
    if (vkEndCommandBuffer(compCommandBuffer.v()) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }
}

void VkRenderer::recordRTCommandBuffers() {
    const std::vector<VkDescriptorSet> sets = m_descs->getSets(descriptorsets::PASSES::RT);

    pipeline::PipelineData rtPipe = m_pipe->getRTPipe();

    VkCommandBufferInheritanceInfo inheritInfo{};
    inheritInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
    inheritInfo.renderPass = VK_NULL_HANDLE;
    inheritInfo.framebuffer = VK_NULL_HANDLE;
    inheritInfo.subpass = 0;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
    beginInfo.pInheritanceInfo = &inheritInfo;

    VkCommandBuffer& commandBuffer = m_rtCB.primary[m_currentFrame].v();
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording rt command buffer!");
    }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipe.pipeline.v());
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, rtPipe.layout.v(), 0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    vkCmdPushConstants(commandBuffer, rtPipe.layout.v(), VK_SHADER_STAGE_RAYGEN_BIT_KHR, 0, sizeof(pushconstants::FramePushConst), &m_framePushConst);
    vkCmdPushConstants(commandBuffer, rtPipe.layout.v(), VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR, sizeof(pushconstants::FramePushConst), sizeof(pushconstants::RTPushConst), &m_rtPushConst);

    vkhfp::vkCmdTraceRaysKHR(commandBuffer, m_raytracing->getRaygenRegion(), m_raytracing->getMissRegion(), m_raytracing->getHitRegion(), m_raytracing->getCallableRegion(), m_swap->getWidth(), m_swap->getHeight(), 1);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record rt command buffer!");
    }
}

void VkRenderer::updatePushConstants() noexcept {
    m_framePushConst.frame = static_cast<int>(m_currentFrame);

    m_lightPushConst.frameCount = static_cast<int>(m_swap->getMaxFrames());
    m_lightPushConst.lightCount = static_cast<int>(m_scene->getLightCount());
    m_lightPushConst.lightsPerBatch = cfg::LIGHTS_PER_BATCH;

    if (m_rtEnabled) {
        m_rtPushConst.frame = m_framePushConst.frame;
        m_rtPushConst.lightCount = m_lightPushConst.lightCount;
    }
}

void VkRenderer::VkRenderer::recordAllCommandBuffers() {
    updatePushConstants();

    if (m_rtEnabled) {
        recordRTCommandBuffers();
    } else {
        recordDeferredCommandBuffers();
        recordShadowCommandBuffers();
        recordLightingCommandBuffers();
        recordWBOITCommandBuffers();
    }

    recordCompCommandBuffers();
}
}  // namespace renderer
