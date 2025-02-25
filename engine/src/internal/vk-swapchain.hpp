#pragma once

#include <vulkan/vulkan.h>

#include <vector>

#include "libraries/vkhelper.hpp"
#include "structures/core.hpp"

namespace swapchain {
class VkSwapChain {
public:
    // delete copying and moving
    VkSwapChain() = default;
    VkSwapChain(const VkSwapChain&) = delete;
    VkSwapChain& operator=(const VkSwapChain&) = delete;
    VkSwapChain(VkSwapChain&&) = delete;
    VkSwapChain& operator=(VkSwapChain&&) = delete;

    void createSwap(core::VkCore& core, uint32_t graphicsFamily);
    void reset() { m_swapChain.reset(); }

    // getters
    [[nodiscard]] VkFormat getFormat() const noexcept { return m_imageFormat; }
    [[nodiscard]] const VkViewport* getViewport() const noexcept { return &m_viewport; }
    [[nodiscard]] VkImageView getImageView(size_t index) const noexcept { return m_imageViews[index].v(); }

    [[nodiscard]] VkSwapchainKHR getSwap() const noexcept { return m_swapChain.v(); }
    [[nodiscard]] const VkSwapchainKHR* getSwapP() const noexcept { return m_swapChain.p(); }

    [[nodiscard]] VkExtent2D getExtent() const noexcept { return m_extent; }
    [[nodiscard]] uint32_t getWidth() const noexcept { return m_extent.width; }
    [[nodiscard]] uint32_t getHeight() const noexcept { return m_extent.height; }

    [[nodiscard]] uint32_t getImageCount() const noexcept { return m_imageCount; }
    [[nodiscard]] constexpr uint32_t getMaxFrames() const noexcept { return maxFrames; }

    [[nodiscard]] uint32_t getImageIndex() const noexcept { return m_imageIndex; }
    [[nodiscard]] uint32_t* getImageIndexP() noexcept { return &m_imageIndex; }

private:
    static constexpr uint32_t maxFrames = 3;

    VkhSwapchainKHR m_swapChain{};

    std::vector<VkImage> m_images;
    std::vector<VkhImageView> m_imageViews;

    VkFormat m_imageFormat = VK_FORMAT_UNDEFINED;
    VkViewport m_viewport{};
    VkExtent2D m_extent{};

    uint32_t m_imageCount = 0;
    uint32_t m_imageIndex = 0;
};
}  // namespace swapchain
