#include "visage.hpp"

#include <stdexcept>

namespace visage {
void Visage::loadModel(const std::string& file, const dml::vec3& pos, const dml::vec3& scale, const dml::vec4& quat) {
    if (m_engineInitialized) {
        throw std::runtime_error("Cannot load new model from file once engine has been started!");
    }

    // check if the model has already been loaded
    for (const scene::ModelData& m : m_modelData) {
        if (m.file == file) {
            utils::logWarning("Model: " + file + " has already been loaded!");
            return;
        }
    }

    m_modelData.emplace_back(file, pos, scale, quat);
}

void Visage::loadModel(const std::string& file, const dml::vec3& pos, const dml::vec3& scale) {
    loadModel(file, pos, scale, {0.0f, 0.0f, 0.0f, 1.0f});
}

void Visage::loadModel(const std::string& file, const dml::vec3& pos, float scale) {
    loadModel(file, pos, {scale, scale, scale}, {0.0f, 0.0f, 0.0f, 1.0f});
}

void Visage::initialize() {
    initGLFW();

    // init Vulkan components
    auto now = utils::now();

    // init core vulkan components
    m_vulkanCore = m_setup.init(m_window);
    m_rtEnabled &= m_setup.isRaytracingSupported();

    // create swapchain
    m_swap.createSwap(m_vulkanCore, m_setup.getGraphicsFamily());

    // init renderer
    m_renderer.init(m_rtEnabled, m_showDebugInfo, m_vulkanCore.device, &m_setup, &m_swap, &m_textures, &m_scene, &m_buffers, &m_descs, &m_pipe, &m_raytracing);
    VkhCommandPool commandPool = m_renderer.getCommandPool();

    // load scene data
    m_scene.init(m_rtEnabled, m_vulkanCore.device, commandPool, m_setup.gQueue());
    m_scene.loadScene(m_modelData);

    // init textures
    m_textures.init(commandPool, m_setup.gQueue(), &m_swap, &m_scene);
    m_textures.loadMeshTextures();
    m_textures.createRenderTextures(m_rtEnabled, true);

    if (m_skybox.empty()) {
        throw std::runtime_error("Skybox must be provided!");
    } else {
        m_textures.loadSkybox(m_skybox);
    }

    // setup acceleration structures if raytracing is enabled
    if (m_rtEnabled) {
        m_raytracing.init(m_swap.getMaxFrames(), commandPool, m_setup.gQueue(), m_vulkanCore.device, &m_scene, &m_textures);
        m_raytracing.createAccelStructures();
    }

    // initalize scene data
    m_scene.initSceneData(0.0f, 0.0f, m_swap.getWidth(), m_swap.getHeight());

    // create buffers from scene data
    m_buffers.init(commandPool, m_setup.gQueue(), m_rtEnabled, m_swap.getMaxFrames(), &m_scene);
    m_buffers.createBuffers(m_currentFrame);

    // init the descriptorsets
    m_descs.init(m_rtEnabled, m_swap.getMaxFrames(), m_vulkanCore.device, &m_scene, &m_textures, &m_buffers, m_raytracing.tlasData(m_rtEnabled));

    // init the pipelines
    m_pipe.init(m_rtEnabled, m_vulkanCore.device, &m_swap, &m_textures, &m_descs);
    m_pipe.createPipelines(true);

    // create the shader binding table if raytracing is enabled
    if (m_rtEnabled) {
        m_raytracing.createSBT(m_pipe.getRTPipe().pipeline, m_setup.getRtProperties());
    }

    // setup imgui
    imguiSetup();

    // setup the framebuffers and command buffers
    m_renderer.createFrameBuffers(true);
    m_renderer.createCommandBuffers();

    // log duration it took to initialize engine
    auto duration = utils::duration<milliseconds>(now);
    std::cout << "Visage initialized in: " << utils::durationString(duration) << "\n";
    utils::sep();

    m_engineInitialized = true;
}

void Visage::render() {
    MouseObject* mouse = MouseSingleton::v().getMouse();
    m_mouseUp = mouse->upAngle;
    m_mouseRight = mouse->rightAngle;

    m_scene.updateCamQuaternion(m_mouseUp, m_mouseRight);

    calcFps();
    glfwPollEvents();
    drawFrame();
}

void Visage::lockMouse(bool locked) {
    MouseObject* mouse = MouseSingleton::v().getMouse();
    mouse->locked = locked;

    if (locked) {
        // set cursor pos to center of the screen
        VkExtent2D swapExtent = m_swap.getExtent();
        mouse->lastX = static_cast<float>(swapExtent.width) / 2.0f;
        mouse->lastY = static_cast<float>(swapExtent.height) / 2.0f;
        glfwSetCursorPos(m_window, mouse->lastX, mouse->lastY);

        // lock and set callback for mouse
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetCursorPosCallback(m_window, mouseCallback);
    } else {
        glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
}

void Visage::translateCamForward(float speed) {
    dml::vec3 forward = m_scene.getCamForward();

    dml::vec3& pos = m_scene.getCamPos();
    pos += forward * speed;
}

void Visage::translateCamRight(float speed) {
    dml::vec3 forward = m_scene.getCamForward();
    dml::vec3 right = m_scene.getCamRight(forward);

    dml::vec3& pos = m_scene.getCamPos();
    pos += right * speed;
}

void Visage::translateCamVertically(float speed) {
    dml::vec3& pos = m_scene.getCamPos();
    pos.y += speed;
}

void Visage::copyModel(const std::string& fileName) {
    const dml::mat4& view = m_scene.getCamMatrices()->view;
    dml::vec3 pos = dml::getCamWorldPos(view);

    bool copied = m_scene.copyModel(pos, fileName, {0.4f, 0.4f, 0.4f}, {0.0f, 0.0f, 0.0f, 1.0f});

    if (copied) {
        m_buffers.updateSceneIndirectCommandsBuffer();

        if (m_rtEnabled) {
            m_raytracing.updateTLAS(m_currentFrame, true);
        }

        m_scene.calcTexIndices();
        m_buffers.createTexIndicesBuffer();
    }
}

void Visage::createLight(float range) {
    dml::vec3 pos = m_scene.getCamWorldPos();
    dml::vec3 target = pos + m_scene.getCamForward();

    createLight(pos, target, range);
}

void Visage::createLight(const dml::vec3& pos, const dml::vec3& target, float range) {
    size_t currentLightCount = m_scene.getLightCount();
    size_t newLightCount = currentLightCount + 1;
    if (newLightCount > cfg::MAX_LIGHTS) return;

    if (m_engineInitialized) {
        vkWaitForFences(m_vulkanCore.device, 1, m_renderer.getFence(m_currentFrame), VK_TRUE, UINT64_MAX);

        if (!m_rtEnabled && m_textures.newShadowBatchNeeded(currentLightCount, newLightCount)) {
            m_textures.createNewShadowBatch();

            for (size_t i = 0; i < m_swap.getMaxFrames(); i++) {
                vkh::Texture s = m_textures.getShadowTex(m_scene.getShadowBatchCount(), i);

                m_renderer.addShadowFrameBuffer(s);
                m_renderer.addShadowCommandBuffers();
                m_descs.addShadowInfo(vkh::createDSImageInfo(s.imageView, s.sampler));
            }

            m_descs.updateLightDS();
        }
    }

    m_scene.createLight(pos, target, range);
}

void Visage::createPlayerLight(float range) {
    m_scene.createPlayerLight(range);
}

void Visage::resetScene() {
    vkWaitForFences(m_vulkanCore.device, 1, m_renderer.getFence(m_currentFrame), VK_TRUE, UINT64_MAX);

    m_scene.resetLights();

    uint32_t maxFrames = m_swap.getMaxFrames();

    if (!m_rtEnabled) {
        m_renderer.reallocateLights();

        m_descs.clearShadowInfos(maxFrames);

        m_textures.resetShadowTextures();

        for (size_t i = 0; i < maxFrames; i++) {
            const vkh::Texture& tex = m_textures.getShadowTex(m_scene.getShadowBatchCount() - 1, i);

            m_descs.addShadowInfo(vkh::createDSImageInfo(tex.imageView, tex.sampler));
            m_renderer.addShadowFrameBuffer(tex);
        }

        m_descs.updateLightDS();
    }

    m_scene.resetObjects();

    if (m_rtEnabled) {
        m_raytracing.updateTLAS(m_currentFrame, true);
    }

    m_scene.calcTexIndices();
    m_buffers.createTexIndicesBuffer();
    m_buffers.updateSceneIndirectCommandsBuffer();
}

void Visage::initGLFW() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);  // enable window resizing

    std::string engineName = "Visage " + cfg::ENGINE_VER;

    m_window = glfwCreateWindow(cfg::SCREEN_WIDTH, cfg::SCREEN_HEIGHT, engineName.c_str(), nullptr, nullptr);

    // imgui initialization
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForVulkan(m_window, true);
}

void Visage::imguiDSLayout() {
    VkDescriptorSetLayoutBinding imguiBinding{};
    imguiBinding.binding = 0;
    imguiBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    imguiBinding.descriptorCount = 1;
    imguiBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;  // access through the fragment shader

    VkDescriptorSetLayoutCreateInfo imguiLayoutInfo{};
    imguiLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    imguiLayoutInfo.bindingCount = 1;
    imguiLayoutInfo.pBindings = &imguiBinding;

    if (vkCreateDescriptorSetLayout(m_vulkanCore.device, &imguiLayoutInfo, nullptr, m_imguiDescriptorSetLayout.p()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor set layout!");
    }
}

void Visage::imguiDSPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;  // descriptor sets can be freed individually
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(m_vulkanCore.device, &poolInfo, nullptr, m_imguiDescriptorPool.p()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Imgui descriptor pool!");
    }
}

void Visage::imguiCheckResult(VkResult err) {
    if (err == 0) return;
    std::cerr << "VkResult is " << err << " in " << __FILE__ << " at line " << __LINE__ << "\n";
    assert(err == 0);
}

void Visage::imguiSetup() {
    std::string path = cfg::FONT_DIR + std::string("OpenSans/OpenSans-VariableFont_wdth,wght.ttf");
    ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 50.0f);

    // descriptor set creation for imgui
    imguiDSLayout();
    imguiDSPool();

    // imgui setup
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = m_vulkanCore.instance;
    initInfo.PhysicalDevice = m_vulkanCore.physicalDevice;
    initInfo.Device = m_vulkanCore.device;
    initInfo.QueueFamily = m_setup.getGraphicsFamily();
    initInfo.Queue = m_setup.gQueue();
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = m_imguiDescriptorPool.v();
    initInfo.Allocator = VK_NULL_HANDLE;
    initInfo.MinImageCount = m_swap.getImageCount();
    initInfo.ImageCount = m_swap.getImageCount();
    initInfo.CheckVkResultFn = imguiCheckResult;
    initInfo.MSAASamples = m_textures.getCompSampleCount();
    initInfo.RenderPass = m_pipe.getCompPipe().renderPass.v();

    ImGui_ImplVulkan_Init(&initInfo);

    VkhCommandPool guiCommandPool = vkh::createCommandPool(m_setup.getGraphicsFamily(), VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT);
    VkhCommandBuffer guiCommandBuffer = vkh::beginSingleTimeCommands(guiCommandPool);
    ImGui_ImplVulkan_CreateFontsTexture();
    vkh::endSingleTimeCommands(guiCommandBuffer, guiCommandPool, m_setup.gQueue());
    ImGui_ImplVulkan_DestroyFontsTexture();
}

void Visage::imguiCleanup() {
    ImGui_ImplGlfw_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();
}

void Visage::calcFps() {
    static auto lastUpdated = utils::now();

    // num of frames since fps was last updated
    static size_t frameCount = 0;
    frameCount++;

    // time since fps was last updated
    double elapsed = utils::duration<milliseconds>(lastUpdated).count() / 1000.0;

    // calculate FPS every 200ms
    if (elapsed >= 0.2) {
        m_fps = static_cast<uint32_t>(frameCount / elapsed);
        frameCount = 0;
        lastUpdated = utils::now();
    }
}

void Visage::recreateSwap() {
    std::cout << "Recreating swap chain...\n";

    int width = 0, height = 0;
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(m_window, &width, &height);
        glfwWaitEvents();
    }

    vkWaitForFences(m_vulkanCore.device, 1, m_renderer.getFence(m_currentFrame), VK_TRUE, UINT64_MAX);
    vkDeviceWaitIdle(m_vulkanCore.device);  // wait for the device to be idle

    m_swap.reset();
    m_swap.createSwap(m_vulkanCore, m_setup.getGraphicsFamily());

    m_textures.createRenderTextures(m_rtEnabled, false);

    // update descriptorsets
    m_descs.update(false, m_raytracing.tlasData(m_rtEnabled));

    // create pipelines
    m_pipe.createPipelines(false);

    // create framebuffers
    m_renderer.createFrameBuffers(false);
}

void Visage::drawFrame() {
    // get next frame
    m_currentFrame = (m_currentFrame + 1) % m_swap.getMaxFrames();

    // wait for and reset fences
    vkWaitForFences(m_vulkanCore.device, 1, m_renderer.getFence(m_currentFrame), VK_TRUE, UINT64_MAX);
    vkResetFences(m_vulkanCore.device, 1, m_renderer.getFence(m_currentFrame));

    // acquire the next image from the swapchain
    VkResult acquireNextImageResult = vkAcquireNextImageKHR(m_vulkanCore.device, m_swap.getSwap(), UINT64_MAX, m_renderer.getImageAvailableSemaphore(m_currentFrame), VK_NULL_HANDLE, m_swap.getImageIndexP());
    if (acquireNextImageResult != VK_SUCCESS && acquireNextImageResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swap chain image!");
    }

    // update buffers
    m_scene.updateSceneData(m_mouseUp, m_mouseRight, m_swap.getWidth(), m_swap.getHeight());
    m_buffers.update(m_currentFrame);

    // update TLAS if raytracing is enabled
    if (m_rtEnabled) {
        m_raytracing.updateTLAS(m_currentFrame, false);
    }

    // record command buffers and draw the frame
    VkResult drawFrameResult = m_renderer.drawFrame(m_currentFrame, static_cast<float>(m_fps));

    // check if the swap chain is out of date (window was resized, etc):
    if (drawFrameResult == VK_ERROR_OUT_OF_DATE_KHR || drawFrameResult == VK_SUBOPTIMAL_KHR) {
        vkDeviceWaitIdle(m_vulkanCore.device);
        recreateSwap();
    } else if (drawFrameResult != VK_SUCCESS) {
        throw std::runtime_error("failed to present swap chain image!");
    }
}
}  // namespace visage
