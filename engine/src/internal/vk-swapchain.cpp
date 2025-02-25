#include "vk-swapchain.hpp"

#include "config.hpp"

namespace swapchain {
void VkSwapChain::createSwap(core::VkCore& core, uint32_t graphicsFamily) {
    vkh::SCsupportDetails swapChainSupport = vkh::querySCsupport();

    // choose the best surface format, present mode, and swap extent for the swap chain
    VkSurfaceFormatKHR surfaceFormat = vkh::chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR present = vkh::chooseSwapPresentMode(swapChainSupport.presentModes);
    m_extent = vkh::chooseSwapExtent(swapChainSupport.capabilities, cfg::SCREEN_WIDTH, cfg::SCREEN_HEIGHT);

    // get the number of images for the swap chain
    m_imageCount = swapChainSupport.capabilities.minImageCount + 1;

    // ensure the image count doesnt go over the max image count
    if (m_imageCount > swapChainSupport.capabilities.maxImageCount) m_imageCount = swapChainSupport.capabilities.maxImageCount;

    m_imageFormat = surfaceFormat.format;

    // create swap chain
    VkSwapchainCreateInfoKHR swapInfo{};
    swapInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapInfo.surface = core.surface;
    swapInfo.minImageCount = m_imageCount;
    swapInfo.imageArrayLayers = 1;
    swapInfo.imageFormat = surfaceFormat.format;
    swapInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapInfo.imageExtent = m_extent;
    swapInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // allows only 1 queue family to access the sc at a time
    // this reduces synchronization overhead
    swapInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapInfo.queueFamilyIndexCount = 1;

    swapInfo.pQueueFamilyIndices = &graphicsFamily;
    swapInfo.preTransform = swapChainSupport.capabilities.currentTransform;  // transform to apply to the swap chain before presentation
    swapInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;             // set the alpha channel to opaque when compositing the final image
    swapInfo.presentMode = present;
    swapInfo.clipped = VK_TRUE;  // if the window is obscured, the pixels that are obscured will not be drawn to
    swapInfo.oldSwapchain = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(core.device, &swapInfo, nullptr, m_swapChain.p()) != VK_SUCCESS) {
        throw std::runtime_error("failed to create swap chain!");
    }

    // get the swap chain image count
    vkGetSwapchainImagesKHR(core.device, m_swapChain.v(), &m_imageCount, nullptr);

    // get the swap chain images
    m_images.resize(m_imageCount);
    vkGetSwapchainImagesKHR(core.device, m_swapChain.v(), &m_imageCount, m_images.data());

    // create the image views
    m_imageViews.clear();
    m_imageViews.resize(m_imageCount);
    for (size_t i = 0; i < m_imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.image = m_images[i];
        viewInfo.format = m_imageFormat;

        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        VkResult result = vkCreateImageView(core.device, &viewInfo, nullptr, m_imageViews[i].p());
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views for the swapchain!!");
        }
    }

    // create the viewport for the swap chain
    m_viewport.x = 0.0f;
    m_viewport.y = 0.0f;
    m_viewport.width = static_cast<float>(m_extent.width);
    m_viewport.height = static_cast<float>(m_extent.height);
    m_viewport.minDepth = 0.0f;
    m_viewport.maxDepth = 1.0f;
}
}  // namespace swapchain
