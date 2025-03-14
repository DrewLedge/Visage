#include "vk-pipelines.hpp"

#include <cstddef>
#include <fstream>

#include "config.hpp"
#include "structures/pushconstants.hpp"

namespace pipelines {
void VkPipelines::init(bool rtEnabled, VkDevice device, const swapchain::VkSwapChain* swap, const textures::VkTextures* textures, const descriptorsets::VkDescriptorSets* descs) noexcept {
    m_rtEnabled = rtEnabled;
    m_device = device;
    m_swap = swap;
    m_textures = textures;
    m_descs = descs;
}

void VkPipelines::createPipelines(bool createShadow) {
    if (m_rtEnabled) {
        createRayTracingPipeline();
    } else {
        getObjectVertInputAttrDescriptions();

        createDeferredPipeline();
        createLightingPipeline();
        createSkyboxPipeline();

        if (createShadow) {
            createShadowPipeline();
        }

        createWBOITPipeline();
    }

    createCompositionPipeline();
}

std::vector<char> VkPipelines::readFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);  // ate means start reading at the end of the file and binary means read the file as binary
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open file: " + filename);
    }
    size_t fileSize = static_cast<size_t>(file.tellg());  // tellg gets the position of the read/write head
    std::vector<char> buffer(fileSize);
    file.seekg(0);  // seekg sets the position of the read/write head
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkhShaderModule VkPipelines::createShaderMod(const std::string& name) const {
    std::vector<char> shaderCode = readFile(cfg::SHADER_DIR + name + std::string(".spv"));
    return vkh::createShaderModule(shaderCode);
}

void VkPipelines::getObjectVertInputAttrDescriptions() {
    const std::array<VkFormat, 4> formats = {
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT,
        VK_FORMAT_R32G32B32_SFLOAT};

    const std::array<size_t, 4> offsets = {
        offsetof(dvl::Vertex, pos),
        offsetof(dvl::Vertex, tex),
        offsetof(dvl::Vertex, normal),
        offsetof(dvl::Vertex, tangent)};

    for (uint32_t i = 0; i < formats.size(); i++) {
        m_objectInputAttrDesc[i] = vkh::vertInputAttrDesc(formats[i], 0, i, offsets[i]);
    }

    // pass the model matrix as a per-instance data
    // seperate the matrix into 4 vec4's so it can be quickly passed and processed
    for (uint32_t i = 0; i < 4; i++) {
        uint32_t index = 4 + i;
        size_t offset = offsetof(instancing::ObjectInstance, model) + sizeof(float) * 4 * i;

        m_objectInputAttrDesc[index] = vkh::vertInputAttrDesc(VK_FORMAT_R32G32B32A32_SFLOAT, 1, index, offset);
    }

    m_objectInputAttrDesc[8] = vkh::vertInputAttrDesc(VK_FORMAT_R32_UINT, 1, 8, offsetof(instancing::ObjectInstance, objectIndex));
}

void VkPipelines::createDeferredPipeline() {
    m_deferredPipeline.reset();

    VkhShaderModule vertShaderModule = createShaderMod("deferred.vert");
    VkhShaderModule fragShaderModule = createShaderMod("deferred.frag");

    VkPipelineShaderStageCreateInfo vertStage = vkh::createShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    VkPipelineShaderStageCreateInfo fragStage = vkh::createShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    VkVertexInputBindingDescription vertBindDesc = vkh::vertInputBindDesc(0, sizeof(dvl::Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
    VkVertexInputBindingDescription instanceBindDesc = vkh::vertInputBindDesc(1, sizeof(instancing::ObjectInstance), VK_VERTEX_INPUT_RATE_INSTANCE);
    std::array<VkVertexInputBindingDescription, 2> bindDesc = {vertBindDesc, instanceBindDesc};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkh::vertInputInfo(bindDesc.data(), bindDesc.size(), m_objectInputAttrDesc.data(), m_objectInputAttrDesc.size());

    VkPipelineInputAssemblyStateCreateInfo inputAssem{};
    inputAssem.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssem.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssem.primitiveRestartEnable = VK_FALSE;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swap->getExtent();

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = m_swap->getViewport();
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;

    VkPipelineMultisampleStateCreateInfo multiSamp{};
    multiSamp.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multiSamp.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multiSamp.alphaToCoverageEnable = VK_FALSE;
    multiSamp.alphaToOneEnable = VK_FALSE;
    multiSamp.sampleShadingEnable = VK_FALSE;
    multiSamp.minSampleShading = 1.0f;

    VkPipelineDepthStencilStateCreateInfo dStencil{};
    dStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dStencil.depthTestEnable = VK_TRUE;
    dStencil.depthWriteEnable = VK_TRUE;
    dStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    dStencil.depthBoundsTestEnable = VK_FALSE;
    dStencil.minDepthBounds = 0.0f;
    dStencil.maxDepthBounds = 1.0f;
    dStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBA{};
    colorBA.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBA.blendEnable = VK_FALSE;

    std::array<VkPipelineColorBlendAttachmentState, 4> blendAttachments{};
    blendAttachments.fill(colorBA);

    VkPipelineColorBlendStateCreateInfo colorBS{};
    colorBS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBS.logicOpEnable = VK_FALSE;
    colorBS.logicOp = VK_LOGIC_OP_COPY;
    colorBS.attachmentCount = 4;
    colorBS.pAttachments = blendAttachments.data();

    VkPushConstantRange framePCRange{};
    framePCRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    framePCRange.offset = 0;
    framePCRange.size = sizeof(pushconstants::FramePushConst);

    const std::vector<VkDescriptorSetLayout> layouts = m_descs->getLayouts(descriptorsets::PASSES::DEFERRED);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pPushConstantRanges = &framePCRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, m_deferredPipeline.layout.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!!");
    }

    std::array<VkAttachmentDescription, 5> attachments{};
    std::array<VkAttachmentReference, 4> colReferences{};

    for (uint8_t i = 0; i < 4; i++) {
        VkAttachmentDescription& a = attachments[i];
        a.format = m_textures->getDeferredColorFormat(i);
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference& ref = colReferences[i];
        ref.attachment = i;
        ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    attachments[4].format = m_textures->getDepthFormat();
    attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[4].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 4;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 4;
    subpass.pColorAttachments = colReferences.data();
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkResult renderPassResult = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, m_deferredPipeline.renderPass.p());
    if (renderPassResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssem;
    pipelineInfo.pViewportState = &vpState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multiSamp;
    pipelineInfo.pDepthStencilState = &dStencil;
    pipelineInfo.pColorBlendState = &colorBS;
    pipelineInfo.layout = m_deferredPipeline.layout.v();
    pipelineInfo.renderPass = m_deferredPipeline.renderPass.v();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;
    VkResult pipelineResult = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_deferredPipeline.pipeline.p());
    if (pipelineResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
}

void VkPipelines::createLightingPipeline() {
    m_lightingPipeline.reset();

    VkhShaderModule vertShaderModule = createShaderMod("lighting.vert");
    VkhShaderModule fragShaderModule = createShaderMod("lighting.frag");

    VkPipelineShaderStageCreateInfo vertStage = vkh::createShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    VkPipelineShaderStageCreateInfo fragStage = vkh::createShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkh::vertInputInfo(nullptr, 0, nullptr, 0);

    // input assembly setup: assembles the vertices into primitives
    // in this case, the primitives represent triangles
    VkPipelineInputAssemblyStateCreateInfo inputAssem{};
    inputAssem.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssem.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssem.primitiveRestartEnable = VK_FALSE;

    // scissor setup: defines a region of the framebuffer in which rendering is allowed to happen
    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swap->getExtent();

    // viewport state: defines how the rendered output is mapped to the framebuffer
    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = m_swap->getViewport();
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    // rasterizer setup: transforms primitives into into fragments to display on the screen
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;                  // if true, fragments that are beyond the near and far planes are clamped
    rasterizer.rasterizerDiscardEnable = VK_FALSE;           // if true, geometry never passes through the rasterizer and all primitives would be discarded
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;           // fill the area of the poly with fragments
    rasterizer.lineWidth = 1.0f;                             // line width the rasterizer draws primitives with
    rasterizer.cullMode = VK_CULL_MODE_NONE;                 // disable culling
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;  // polygons with vertices in counter clockwise order will be considered front facing
    rasterizer.depthBiasEnable = VK_FALSE;                   // disable depth bias on fragments

    // multisampling setup: samples multiple points in each pixel and combines them to reduce jagged and blunt edges
    VkPipelineMultisampleStateCreateInfo multiSamp{};
    multiSamp.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multiSamp.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;  // number of samples to use per fragment (1 = no mulisampling)
    multiSamp.alphaToCoverageEnable = VK_TRUE;               // enables alpha-to-coverage, which uses the alpha component to determine the sample coverage
    multiSamp.alphaToOneEnable = VK_FALSE;                   // if enabled, forces the alpha component of the color attachment to 1
    multiSamp.sampleShadingEnable = VK_FALSE;                // if enabled, would force per sample shading instad of per fragment shading
    multiSamp.minSampleShading = 1.0f;                       // min fraction for sample shading; closer to one is smoother

    // depth and stencil testing setup: allows for fragments to be discarded based on depth and stencil values
    VkPipelineDepthStencilStateCreateInfo dStencil{};
    dStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dStencil.depthTestEnable = VK_FALSE;        // disable depth testing
    dStencil.depthWriteEnable = VK_FALSE;       // disable writing to the depth buffer
    dStencil.depthBoundsTestEnable = VK_FALSE;  // if true, discards fragments whose depth values fall outside the min and max bounds
    dStencil.minDepthBounds = 0.0f;             // min depth bound
    dStencil.maxDepthBounds = 1.0f;             // max depth bound
    dStencil.stencilTestEnable = VK_FALSE;      // disable stencil testing

    // color blend attachment: tells the gpu how the outputted color from the frag shader will be combined with the data in the framebuffer
    VkPipelineColorBlendAttachmentState colorBA{};
    colorBA.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;  // color channels to apply the blending operation to
    colorBA.blendEnable = VK_FALSE;                                                                                                      // disable blending

    // color blend state: global pipeline blend settings
    VkPipelineColorBlendStateCreateInfo colorBS{};
    colorBS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBS.logicOpEnable = VK_FALSE;  // disable logic operations for color blending
    colorBS.logicOp = VK_LOGIC_OP_COPY;
    colorBS.attachmentCount = 1;  // number of color blend attachments
    colorBS.pAttachments = &colorBA;

    VkPushConstantRange framePCRange{};
    framePCRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    framePCRange.offset = 0;
    framePCRange.size = sizeof(pushconstants::FramePushConst);

    VkPushConstantRange lightPCRange{};
    lightPCRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    lightPCRange.offset = sizeof(pushconstants::FramePushConst);
    lightPCRange.size = sizeof(pushconstants::LightPushConst);

    const std::vector<VkDescriptorSetLayout> layouts = m_descs->getLayouts(descriptorsets::PASSES::LIGHTING);
    const std::array<VkPushConstantRange, 2> ranges = {framePCRange, lightPCRange};

    // pipeline layout setup: defines the connection between shader stages and resources
    // this data includes: descriptorsets and push constants
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pPushConstantRanges = ranges.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(ranges.size());

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, m_lightingPipeline.layout.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!!");
    }

    // color attachment: specifies the properties of the color image used in the render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;                  // format of the color attachment
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;                         // number of samples to use for multisampling
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;                    // what to do with the data in the attachment before rendering
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;                  // what to do with the data in the attachment after rendering
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;         // what to do with the stencil data before rendering
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;       // what to do with the stencil data after rendering
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;               // layout of the image before the render pass starts
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // layout of the image after the render pass ends

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;                                     // index of the attachment description in the attachment descriptions array
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;  // layout to use for the attachment during the subpass

    // subpass: a phase of rendering within the render pass which specifies which attachments are used in that phase
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;  // type of pipeline to bind to
    subpass.colorAttachmentCount = 1;                             // num color attachments
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = nullptr;

    // render pass: encapsulates all attachments and subpasses that will be used during rendering
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkResult renderPassResult = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, m_lightingPipeline.renderPass.p());
    if (renderPassResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

    // pipeline setup: the data needed to create the pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssem;
    pipelineInfo.pViewportState = &vpState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multiSamp;
    pipelineInfo.pDepthStencilState = &dStencil;
    pipelineInfo.pColorBlendState = &colorBS;
    pipelineInfo.layout = m_lightingPipeline.layout.v();
    pipelineInfo.renderPass = m_lightingPipeline.renderPass.v();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;  // no base pipeline for now
    pipelineInfo.basePipelineIndex = -1;
    VkResult pipelineResult = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_lightingPipeline.pipeline.p());
    if (pipelineResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }
}

void VkPipelines::createShadowPipeline() {
    m_shadowPipeline.reset();

    VkhShaderModule vertShaderModule = createShaderMod("shadow.vert");
    VkhShaderModule fragShaderModule = createShaderMod("shadow.frag");

    VkPipelineShaderStageCreateInfo vertStage = vkh::createShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    VkPipelineShaderStageCreateInfo fragStage = vkh::createShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    VkVertexInputBindingDescription vertBindDesc = vkh::vertInputBindDesc(0, sizeof(dvl::Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
    VkVertexInputBindingDescription instanceBindDesc = vkh::vertInputBindDesc(1, sizeof(instancing::ObjectInstance), VK_VERTEX_INPUT_RATE_INSTANCE);
    std::array<VkVertexInputBindingDescription, 2> bindDesc = {vertBindDesc, instanceBindDesc};

    std::array<VkVertexInputAttributeDescription, 5> attrDesc{};
    attrDesc[0] = vkh::vertInputAttrDesc(VK_FORMAT_R32G32B32_SFLOAT, 0, 0, offsetof(dvl::Vertex, pos));

    for (uint32_t i = 0; i < 4; i++) {
        uint32_t index = i + 1;
        size_t offset = offsetof(instancing::ObjectInstance, model) + sizeof(float) * 4 * i;

        attrDesc[index] = vkh::vertInputAttrDesc(VK_FORMAT_R32G32B32A32_SFLOAT, 1, index, offset);
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkh::vertInputInfo(bindDesc.data(), bindDesc.size(), attrDesc.data(), attrDesc.size());

    VkPipelineInputAssemblyStateCreateInfo inputAssem{};
    inputAssem.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssem.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssem.primitiveRestartEnable = VK_FALSE;

    VkViewport shadowVP{};
    shadowVP.x = 0.0f;
    shadowVP.y = 0.0f;
    shadowVP.width = static_cast<float>(cfg::SHADOW_WIDTH);
    shadowVP.height = static_cast<float>(cfg::SHADOW_HEIGHT);
    shadowVP.minDepth = 0.0f;
    shadowVP.maxDepth = 1.0f;

    VkRect2D shadowScissor{};
    shadowScissor.offset = {0, 0};
    shadowScissor.extent.width = cfg::SHADOW_WIDTH;
    shadowScissor.extent.height = cfg::SHADOW_HEIGHT;

    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = &shadowVP;
    vpState.scissorCount = 1;
    vpState.pScissors = &shadowScissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_TRUE;
    rasterizer.depthBiasConstantFactor = 1.75f;
    rasterizer.depthBiasSlopeFactor = 1.75f;
    rasterizer.depthBiasClamp = 0.0f;

    VkPipelineMultisampleStateCreateInfo multiSamp{};
    multiSamp.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multiSamp.sampleShadingEnable = VK_FALSE;
    multiSamp.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo dStencil{};
    dStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dStencil.depthTestEnable = VK_TRUE;
    dStencil.depthWriteEnable = VK_TRUE;
    dStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    dStencil.depthBoundsTestEnable = VK_FALSE;
    dStencil.minDepthBounds = 0.0f;
    dStencil.maxDepthBounds = 1.0f;
    dStencil.stencilTestEnable = VK_FALSE;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = m_textures->getDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 0;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    // multiview view mask
    uint32_t viewMask = (1U << cfg::LIGHTS_PER_BATCH) - 1;

    // multiview
    VkRenderPassMultiviewCreateInfo mvInfo{};
    mvInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO;
    mvInfo.subpassCount = 1;
    mvInfo.pViewMasks = &viewMask;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.pNext = &mvInfo;
    if (vkCreateRenderPass(m_device, &renderPassInfo, nullptr, m_shadowPipeline.renderPass.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow map render pass!");
    }

    VkPipelineColorBlendStateCreateInfo colorBS{};
    colorBS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBS.attachmentCount = 0;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(pushconstants::ShadowPushConst);

    const std::vector<VkDescriptorSetLayout> layouts = m_descs->getLayouts(descriptorsets::PASSES::SHADOW);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, m_shadowPipeline.layout.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssem;
    pipelineInfo.pViewportState = &vpState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multiSamp;
    pipelineInfo.pDepthStencilState = &dStencil;
    pipelineInfo.pColorBlendState = &colorBS;
    pipelineInfo.layout = m_shadowPipeline.layout.v();
    pipelineInfo.renderPass = m_shadowPipeline.renderPass.v();
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_shadowPipeline.pipeline.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shadow map pipeline!");
    }
}

void VkPipelines::createSkyboxPipeline() {
    m_skyboxPipeline.reset();

    VkhShaderModule vertShaderModule = createShaderMod("sky.vert");
    VkhShaderModule fragShaderModule = createShaderMod("sky.frag");

    VkPipelineShaderStageCreateInfo vertStage = vkh::createShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    VkPipelineShaderStageCreateInfo fragStage = vkh::createShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkh::vertInputInfo(nullptr, 0, nullptr, 0);

    VkPipelineInputAssemblyStateCreateInfo inputAssem{};
    inputAssem.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssem.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssem.primitiveRestartEnable = VK_FALSE;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swap->getExtent();
    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = m_swap->getViewport();
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multiSamp{};
    multiSamp.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multiSamp.sampleShadingEnable = VK_FALSE;
    multiSamp.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multiSamp.minSampleShading = 1.0f;
    multiSamp.pSampleMask = nullptr;
    multiSamp.alphaToCoverageEnable = VK_FALSE;
    multiSamp.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo dStencil{};
    dStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dStencil.depthTestEnable = VK_FALSE;
    dStencil.depthWriteEnable = VK_FALSE;
    dStencil.depthBoundsTestEnable = VK_FALSE;
    dStencil.minDepthBounds = 0.0f;
    dStencil.maxDepthBounds = 1.0f;
    dStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBA{};
    colorBA.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBA.blendEnable = VK_FALSE;  // disable blending for the skybox
    VkPipelineColorBlendStateCreateInfo colorBS{};
    colorBS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBS.logicOpEnable = VK_FALSE;
    colorBS.logicOp = VK_LOGIC_OP_COPY;
    colorBS.attachmentCount = 1;
    colorBS.pAttachments = &colorBA;

    VkPushConstantRange framePCRange{};
    framePCRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    framePCRange.offset = 0;
    framePCRange.size = sizeof(pushconstants::FramePushConst);

    const std::vector<VkDescriptorSetLayout> layouts = m_descs->getLayouts(descriptorsets::PASSES::SKYBOX);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pPushConstantRanges = &framePCRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, m_skyboxPipeline.layout.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout for skybox!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssem;
    pipelineInfo.pViewportState = &vpState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multiSamp;
    pipelineInfo.pDepthStencilState = &dStencil;
    pipelineInfo.pColorBlendState = &colorBS;
    pipelineInfo.layout = m_skyboxPipeline.layout.v();
    pipelineInfo.renderPass = m_lightingPipeline.renderPass.v();
    pipelineInfo.subpass = 0;
    VkResult pipelineResult = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_skyboxPipeline.pipeline.p());
    if (pipelineResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline for skybox!");
    }
}

void VkPipelines::createWBOITPipeline() {
    m_wboitPipeline.reset();

    VkhShaderModule vertShaderModule = createShaderMod("wboit.vert");
    VkhShaderModule fragShaderModule = createShaderMod("wboit.frag");

    VkPipelineShaderStageCreateInfo vertStage = vkh::createShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    VkPipelineShaderStageCreateInfo fragStage = vkh::createShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    VkVertexInputBindingDescription vertBindDesc = vkh::vertInputBindDesc(0, sizeof(dvl::Vertex), VK_VERTEX_INPUT_RATE_VERTEX);
    VkVertexInputBindingDescription instanceBindDesc = vkh::vertInputBindDesc(1, sizeof(instancing::ObjectInstance), VK_VERTEX_INPUT_RATE_INSTANCE);
    std::array<VkVertexInputBindingDescription, 2> bindDesc = {vertBindDesc, instanceBindDesc};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkh::vertInputInfo(bindDesc.data(), bindDesc.size(), m_objectInputAttrDesc.data(), m_objectInputAttrDesc.size());

    VkPipelineInputAssemblyStateCreateInfo inputAssem{};
    inputAssem.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssem.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssem.primitiveRestartEnable = VK_FALSE;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swap->getExtent();
    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = m_swap->getViewport();
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multiSamp{};
    multiSamp.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multiSamp.sampleShadingEnable = VK_FALSE;
    multiSamp.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multiSamp.minSampleShading = 1.0f;
    multiSamp.pSampleMask = nullptr;
    multiSamp.alphaToCoverageEnable = VK_FALSE;
    multiSamp.alphaToOneEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo dStencil{};
    dStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dStencil.depthTestEnable = VK_TRUE;
    dStencil.depthWriteEnable = VK_FALSE;
    dStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    dStencil.depthBoundsTestEnable = VK_FALSE;
    dStencil.minDepthBounds = 0.0f;
    dStencil.maxDepthBounds = 1.0f;
    dStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBA{};
    colorBA.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBA.blendEnable = VK_TRUE;
    colorBA.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBA.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBA.colorBlendOp = VK_BLEND_OP_ADD;
    colorBA.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBA.dstAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBA.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBS{};
    colorBS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBS.logicOpEnable = VK_FALSE;
    colorBS.logicOp = VK_LOGIC_OP_COPY;
    colorBS.attachmentCount = 1;
    colorBS.pAttachments = &colorBA;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkResult renderPassResult = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, m_wboitPipeline.renderPass.p());
    if (renderPassResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

    VkPushConstantRange framePCRange{};
    framePCRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    framePCRange.offset = 0;
    framePCRange.size = sizeof(pushconstants::FramePushConst);

    VkPushConstantRange objectPCRange{};
    objectPCRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    objectPCRange.offset = sizeof(pushconstants::FramePushConst);
    objectPCRange.size = sizeof(pushconstants::LightPushConst);

    const std::vector<VkDescriptorSetLayout> layouts = m_descs->getLayouts(descriptorsets::PASSES::WBOIT);
    const std::array<VkPushConstantRange, 2> ranges = {framePCRange, objectPCRange};

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pPushConstantRanges = ranges.data();
    pipelineLayoutInfo.pushConstantRangeCount = static_cast<uint32_t>(ranges.size());

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, m_wboitPipeline.layout.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout for WBOIT!!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssem;
    pipelineInfo.pViewportState = &vpState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multiSamp;
    pipelineInfo.pDepthStencilState = &dStencil;
    pipelineInfo.pColorBlendState = &colorBS;
    pipelineInfo.layout = m_wboitPipeline.layout.v();
    pipelineInfo.renderPass = m_wboitPipeline.renderPass.v();
    pipelineInfo.subpass = 0;
    VkResult pipelineResult = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_wboitPipeline.pipeline.p());
    if (pipelineResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline for WBOIT!");
    }
}

void VkPipelines::createCompositionPipeline() {
    m_compPipeline.reset();

    std::string vert;
    std::string frag;
    if (m_rtEnabled) {
        vert = "present.vert";
        frag = "present.frag";
    } else {
        vert = "composition.vert";
        frag = "composition.frag";
    }

    VkhShaderModule vertShaderModule = createShaderMod(vert);
    VkhShaderModule fragShaderModule = createShaderMod(frag);

    VkPipelineShaderStageCreateInfo vertStage = vkh::createShaderStage(VK_SHADER_STAGE_VERTEX_BIT, vertShaderModule);
    VkPipelineShaderStageCreateInfo fragStage = vkh::createShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule);
    std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo = vkh::vertInputInfo(nullptr, 0, nullptr, 0);

    VkPipelineInputAssemblyStateCreateInfo inputAssem{};
    inputAssem.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssem.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssem.primitiveRestartEnable = VK_FALSE;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = m_swap->getExtent();
    VkPipelineViewportStateCreateInfo vpState{};
    vpState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vpState.viewportCount = 1;
    vpState.pViewports = m_swap->getViewport();
    vpState.scissorCount = 1;
    vpState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;
    rasterizer.depthBiasClamp = 0.0f;
    rasterizer.depthBiasSlopeFactor = 0.0f;

    VkPipelineMultisampleStateCreateInfo multiSamp{};
    multiSamp.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multiSamp.rasterizationSamples = m_textures->getCompSampleCount();
    multiSamp.alphaToCoverageEnable = VK_FALSE;
    multiSamp.alphaToOneEnable = VK_FALSE;
    multiSamp.sampleShadingEnable = VK_TRUE;
    multiSamp.minSampleShading = 0.2f;

    VkPipelineDepthStencilStateCreateInfo dStencil{};
    dStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    dStencil.depthTestEnable = VK_FALSE;
    dStencil.depthWriteEnable = VK_FALSE;
    dStencil.depthBoundsTestEnable = VK_FALSE;
    dStencil.minDepthBounds = 0.0f;
    dStencil.maxDepthBounds = 1.0f;
    dStencil.stencilTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState colorBA{};
    colorBA.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBA.blendEnable = VK_TRUE;
    colorBA.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBA.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBA.colorBlendOp = VK_BLEND_OP_ADD;
    colorBA.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBA.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    colorBA.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBS{};
    colorBS.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBS.logicOpEnable = VK_FALSE;
    colorBS.logicOp = VK_LOGIC_OP_COPY;
    colorBS.attachmentCount = 1;
    colorBS.pAttachments = &colorBA;

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_swap->getFormat();
    colorAttachment.samples = m_textures->getCompSampleCount();
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription colorResolve{};
    colorResolve.format = m_swap->getFormat();
    colorResolve.samples = VK_SAMPLE_COUNT_1_BIT;
    colorResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorResolveAttachmentRef{};
    colorResolveAttachmentRef.attachment = 1;
    colorResolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pResolveAttachments = &colorResolveAttachmentRef;

    std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, colorResolve};
    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    VkResult renderPassResult = vkCreateRenderPass(m_device, &renderPassInfo, nullptr, m_compPipeline.renderPass.p());
    if (renderPassResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pcRange.size = m_rtEnabled ? sizeof(pushconstants::RTPushConst) : sizeof(pushconstants::FramePushConst);
    pcRange.offset = 0;

    const std::vector<VkDescriptorSetLayout> layouts = m_descs->getLayouts(descriptorsets::PASSES::COMP);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = m_rtEnabled ? &layouts[0] : &layouts[1];
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;

    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, m_compPipeline.layout.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout for composition!!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pStages = stages.data();
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssem;
    pipelineInfo.pViewportState = &vpState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multiSamp;
    pipelineInfo.pDepthStencilState = &dStencil;
    pipelineInfo.pColorBlendState = &colorBS;
    pipelineInfo.layout = m_compPipeline.layout.v();
    pipelineInfo.renderPass = m_compPipeline.renderPass.v();
    pipelineInfo.subpass = 0;
    VkResult pipelineResult = vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_compPipeline.pipeline.p());
    if (pipelineResult != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline for the composition pass!");
    }
}

void VkPipelines::createRayTracingPipeline() {
    m_rtPipeline.reset();

    constexpr size_t numShaders = 5;

    std::vector<std::string> shaderNames;
    shaderNames.push_back("gen.rgen");
    shaderNames.push_back("miss.rmiss");
    shaderNames.push_back("shadowmiss.rmiss");
    shaderNames.push_back("closehit.rchit");
    shaderNames.push_back("shadowhit.rchit");

    std::vector<VkShaderStageFlagBits> shaderStageFlagBits;
    shaderStageFlagBits.push_back(VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    shaderStageFlagBits.push_back(VK_SHADER_STAGE_MISS_BIT_KHR);
    shaderStageFlagBits.push_back(VK_SHADER_STAGE_MISS_BIT_KHR);
    shaderStageFlagBits.push_back(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    shaderStageFlagBits.push_back(VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    // populate the shader module and shader stages data
    std::vector<VkhShaderModule> shaderModules;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages{};
    for (uint8_t i = 0; i < numShaders; i++) {
        shaderModules.push_back(createShaderMod(shaderNames[i]));
        shaderStages.push_back(vkh::createShaderStage(shaderStageFlagBits[i], shaderModules[i]));
    }

    std::array<VkRayTracingShaderGroupCreateInfoKHR, numShaders> shaderGroups{};

    // populate the shader group data
    for (uint8_t i = 0; i < numShaders; i++) {
        shaderGroups[i].sType = VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR;
        shaderGroups[i].anyHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[i].closestHitShader = VK_SHADER_UNUSED_KHR;
        shaderGroups[i].intersectionShader = VK_SHADER_UNUSED_KHR;
    }

    // ray generation group
    shaderGroups[0].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[0].generalShader = 0;  // ray gen index

    // ray miss group
    shaderGroups[1].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[1].generalShader = 1;  // ray miss index

    // shadow miss group
    shaderGroups[2].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroups[2].generalShader = 2;

    // ray hit group
    shaderGroups[3].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    shaderGroups[3].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[3].closestHitShader = 3;

    // shadow hit group
    shaderGroups[4].type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    shaderGroups[4].generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroups[4].closestHitShader = 4;

    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
    pcRange.offset = 0;
    pcRange.size = sizeof(pushconstants::RTPushConst);

    // create the pipeline layoyut
    const std::vector<VkDescriptorSetLayout> layouts = m_descs->getLayouts(descriptorsets::PASSES::RT);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pSetLayouts = layouts.data();
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(layouts.size());
    pipelineLayoutInfo.pPushConstantRanges = &pcRange;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    VkResult result = vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, m_rtPipeline.layout.p());
    if (result != VK_SUCCESS) {
        throw std::runtime_error("failed to create raytracing pipeline layout!!");
    }

    // create the pipeline
    VkRayTracingPipelineCreateInfoKHR pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    pipelineInfo.maxPipelineRayRecursionDepth = cfg::MAX_RAY_RECURSION;
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pGroups = shaderGroups.data();
    pipelineInfo.groupCount = static_cast<uint32_t>(shaderGroups.size());
    pipelineInfo.layout = m_rtPipeline.layout.v();
    if (vkhfp::vkCreateRayTracingPipelinesKHR(m_device, VK_NULL_HANDLE, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, m_rtPipeline.pipeline.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ray tracing pipeline!!");
    }
}
}  // namespace pipelines
