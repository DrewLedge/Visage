#pragma once

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>

#include "internal/vk-buffers.hpp"
#include "internal/vk-descriptorsets.hpp"
#include "internal/vk-pipelines.hpp"
#include "internal/vk-raytracing.hpp"
#include "internal/vk-renderer.hpp"
#include "internal/vk-scene.hpp"
#include "internal/vk-setup.hpp"
#include "internal/vk-swapchain.hpp"
#include "internal/vk-textures.hpp"
#include "mouse.hpp"

namespace visage {
class Visage {
public:
    Visage() = default;
    Visage(const Visage&) = delete;
    Visage& operator=(const Visage&) = delete;
    Visage(Visage&&) = delete;
    Visage& operator=(Visage&&) = delete;

    ~Visage() {
        vkDeviceWaitIdle(m_vulkanCore.device);
        imguiCleanup();
    }

    // asset loading
    void loadModel(const std::string& file, const dml::vec3& pos, const dml::vec3& scale, const dml::vec4& quat);
    void loadModel(const std::string& file, const dml::vec3& pos, const dml::vec3& scale);
    void loadModel(const std::string& file, const dml::vec3& pos, float scale);

    void loadSkybox(const std::string& file) noexcept { m_skybox = file; }

    // core
    void initialize();
    [[nodiscard]] bool isRunning() noexcept { return !glfwWindowShouldClose(m_window); }
    void render();

    // camera
    void lockMouse(bool locked);
    void translateCamForward(float speed);
    void translateCamRight(float speed);
    void translateCamVertically(float speed);

    // scene modification
    void copyModel(const std::string& fileName);

    void createLight(const dml::vec3& pos, const dml::vec3& target, float range);
    void createLightAtCamera(float range);
    void createPlayerLight(float range);

    void resetScene();

    // getters
    [[nodiscard]] uint32_t getScreenWidth() const noexcept { return m_swap.getWidth(); }
    [[nodiscard]] uint32_t getScreenHeight() const noexcept { return m_swap.getHeight(); }

    // keyboard
    [[nodiscard]] bool isKeyHeld(int key) const { return glfwGetKey(m_window, key) == GLFW_PRESS; }
    [[nodiscard]] bool isKeyReleased(int key) {
        bool held = isKeyHeld(key);
        bool released = m_prevKeyStates[key] == GLFW_PRESS && !held;
        m_prevKeyStates[key] = held ? GLFW_PRESS : GLFW_RELEASE;

        return released;
    }

    // setters
    void setCursorPos(float x, float y) { glfwSetCursorPos(m_window, x, y); }

    void setMouseSensitivity(float sensitivity) noexcept {
        MouseObject* mouse = MouseSingleton::v().getMouse();
        mouse->sensitivity = sensitivity * 0.1f;
    }

    void enableRaytracing() noexcept { m_rtEnabled = true; }
    void showDebugInfo() noexcept { m_showDebugInfo = true; }

private:
    core::VkCore m_vulkanCore{};
    bool m_engineInitialized = false;

    setup::VkSetup m_setup{};
    swapchain::VkSwapChain m_swap{};
    textures::VkTextures m_textures{};
    scene::VkScene m_scene{};
    buffers::VkBuffers m_buffers{};
    descriptorsets::VkDescriptorSets m_descs{};
    pipelines::VkPipelines m_pipe{};
    raytracing::VkRaytracing m_raytracing{};
    renderer::VkRenderer m_renderer{};

    // frame data
    uint32_t m_currentFrame = 0;
    uint32_t m_maxFrames = 0;
    uint32_t m_fps = 0;

    // descriptor sets and pools
    VkhDescriptorPool m_imguiDescriptorPool{};
    VkhDescriptorSetLayout m_imguiDescriptorSetLayout{};

    // engine data
    std::vector<scene::ModelData> m_modelData;
    std::string m_skybox{};
    bool m_rtEnabled = false;
    bool m_sceneChanged = false;
    bool m_showDebugInfo = false;

    // glfw
    GLFWwindow* m_window = nullptr;
    float m_mouseUp = 0.0f;
    float m_mouseRight = 0.0f;
    std::array<int, GLFW_KEY_LAST + 1> m_prevKeyStates{};

private:
    void initGLFW();

    // imgui
    void imguiDSLayout();
    void imguiDSPool();
    static void imguiCheckResult(VkResult err);
    void imguiSetup();
    void imguiCleanup();

    void calcFps();
    void recreateSwap();
    void drawFrame();
};
}  // namespace visage
