#pragma once

#ifdef USE_VULKAN

#include "VulkanContext.h"
#include <memory>

class VulkanRenderPipeline {
public:
  VulkanRenderPipeline(std::shared_ptr<VulkanContext> context);
  ~VulkanRenderPipeline();

  // Initialize for offscreen rendering (no swapchain needed for export)
  bool Initialize(int width, int height);
  void Cleanup();

  // Render a frame (will be used for timeline rendering)
  bool RenderFrame(void *pixels, int width, int height);

  // Get framebuffer for reading results
  VkImage GetOutputImage() const { return m_OutputImage; }

  bool IsInitialized() const { return m_Initialized; }

private:
  bool CreateRenderPass();
  bool CreateFramebuffer();
  bool CreateCommandPool();
  bool CreateCommandBuffers();
  bool CreateSyncObjects();

  // Image resources for offscreen rendering
  bool CreateOutputImage(int width, int height);
  void DestroyOutputImage();

private:
  std::shared_ptr<VulkanContext> m_Context;

  VkRenderPass m_RenderPass = VK_NULL_HANDLE;
  VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;

  VkCommandPool m_CommandPool = VK_NULL_HANDLE;
  VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

  VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
  VkFence m_InFlightFence = VK_NULL_HANDLE;

  // Output image (for offscreen rendering)
  VkImage m_OutputImage = VK_NULL_HANDLE;
  VkDeviceMemory m_OutputImageMemory = VK_NULL_HANDLE;
  VkImageView m_OutputImageView = VK_NULL_HANDLE;

  int m_Width = 0;
  int m_Height = 0;
  bool m_Initialized = false;
};

#endif // USE_VULKAN
