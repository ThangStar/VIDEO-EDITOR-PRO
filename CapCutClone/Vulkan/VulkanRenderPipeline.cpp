#ifdef USE_VULKAN

#include "VulkanRenderPipeline.h"
#include <array>
#include <iostream>


VulkanRenderPipeline::VulkanRenderPipeline(
    std::shared_ptr<VulkanContext> context)
    : m_Context(context) {}

VulkanRenderPipeline::~VulkanRenderPipeline() { Cleanup(); }

bool VulkanRenderPipeline::Initialize(int width, int height) {
  m_Width = width;
  m_Height = height;

  std::cout << "[VulkanRenderPipeline] Initializing " << width << "x" << height
            << std::endl;

  if (!CreateOutputImage(width, height)) {
    return false;
  }

  if (!CreateRenderPass()) {
    return false;
  }

  if (!CreateFramebuffer()) {
    return false;
  }

  if (!CreateCommandPool()) {
    return false;
  }

  if (!CreateCommandBuffers()) {
    return false;
  }

  if (!CreateSyncObjects()) {
    return false;
  }

  m_Initialized = true;
  std::cout << "[VulkanRenderPipeline] Initialized successfully" << std::endl;
  return true;
}

void VulkanRenderPipeline::Cleanup() {
  if (!m_Initialized)
    return;

  VkDevice device = m_Context->GetDevice();

  vkDeviceWaitIdle(device);

  if (m_InFlightFence != VK_NULL_HANDLE) {
    vkDestroyFence(device, m_InFlightFence, nullptr);
    m_InFlightFence = VK_NULL_HANDLE;
  }

  if (m_RenderFinishedSemaphore != VK_NULL_HANDLE) {
    vkDestroySemaphore(device, m_RenderFinishedSemaphore, nullptr);
    m_RenderFinishedSemaphore = VK_NULL_HANDLE;
  }

  if (m_CommandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, m_CommandPool, nullptr);
    m_CommandPool = VK_NULL_HANDLE;
  }

  if (m_Framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device, m_Framebuffer, nullptr);
    m_Framebuffer = VK_NULL_HANDLE;
  }

  if (m_RenderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, m_RenderPass, nullptr);
    m_RenderPass = VK_NULL_HANDLE;
  }

  DestroyOutputImage();

  m_Initialized = false;
  std::cout << "[VulkanRenderPipeline] Cleaned up" << std::endl;
}

bool VulkanRenderPipeline::CreateOutputImage(int width, int height) {
  VkDevice device = m_Context->GetDevice();

  // Create image for offscreen rendering (RGBA8)
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = width;
  imageInfo.extent.height = height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateImage(device, &imageInfo, nullptr, &m_OutputImage) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to create output image"
              << std::endl;
    return false;
  }

  // Allocate memory for image
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(device, m_OutputImage, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;

  // Find suitable memory type (device local)
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(m_Context->GetPhysicalDevice(),
                                      &memProperties);

  uint32_t memoryType = 0;
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((memRequirements.memoryTypeBits & (1 << i)) &&
        (memProperties.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      memoryType = i;
      break;
    }
  }
  allocInfo.memoryTypeIndex = memoryType;

  if (vkAllocateMemory(device, &allocInfo, nullptr, &m_OutputImageMemory) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to allocate image memory"
              << std::endl;
    return false;
  }

  vkBindImageMemory(device, m_OutputImage, m_OutputImageMemory, 0);

  // Create image view
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = m_OutputImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &m_OutputImageView) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to create image view"
              << std::endl;
    return false;
  }

  return true;
}

void VulkanRenderPipeline::DestroyOutputImage() {
  VkDevice device = m_Context->GetDevice();

  if (m_OutputImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, m_OutputImageView, nullptr);
    m_OutputImageView = VK_NULL_HANDLE;
  }

  if (m_OutputImage != VK_NULL_HANDLE) {
    vkDestroyImage(device, m_OutputImage, nullptr);
    m_OutputImage = VK_NULL_HANDLE;
  }

  if (m_OutputImageMemory != VK_NULL_HANDLE) {
    vkFreeMemory(device, m_OutputImageMemory, nullptr);
    m_OutputImageMemory = VK_NULL_HANDLE;
  }
}

bool VulkanRenderPipeline::CreateRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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

  if (vkCreateRenderPass(m_Context->GetDevice(), &renderPassInfo, nullptr,
                         &m_RenderPass) != VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to create render pass"
              << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderPipeline::CreateFramebuffer() {
  VkFramebufferCreateInfo framebufferInfo{};
  framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebufferInfo.renderPass = m_RenderPass;
  framebufferInfo.attachmentCount = 1;
  framebufferInfo.pAttachments = &m_OutputImageView;
  framebufferInfo.width = m_Width;
  framebufferInfo.height = m_Height;
  framebufferInfo.layers = 1;

  if (vkCreateFramebuffer(m_Context->GetDevice(), &framebufferInfo, nullptr,
                          &m_Framebuffer) != VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to create framebuffer"
              << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderPipeline::CreateCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = m_Context->GetGraphicsQueueFamily();
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(m_Context->GetDevice(), &poolInfo, nullptr,
                          &m_CommandPool) != VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to create command pool"
              << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderPipeline::CreateCommandBuffers() {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo,
                               &m_CommandBuffer) != VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to allocate command buffers"
              << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderPipeline::CreateSyncObjects() {
  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateSemaphore(m_Context->GetDevice(), &semaphoreInfo, nullptr,
                        &m_RenderFinishedSemaphore) != VK_SUCCESS ||
      vkCreateFence(m_Context->GetDevice(), &fenceInfo, nullptr,
                    &m_InFlightFence) != VK_SUCCESS) {
    std::cerr << "[VulkanRenderPipeline] Failed to create sync objects"
              << std::endl;
    return false;
  }

  return true;
}

bool VulkanRenderPipeline::RenderFrame(void *pixels, int width, int height) {
  // TODO: Implement actual rendering
  // For now, just a placeholder that will be filled in Phase 1.3-1.4
  return true;
}

#endif // USE_VULKAN
