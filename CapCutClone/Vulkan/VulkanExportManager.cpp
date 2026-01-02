#ifdef USE_VULKAN

#include "VulkanExportManager.h"
#include "VulkanComputePipeline.h"
#include "VulkanContext.h"
#include <cstring>
#include <iostream>
#include <vulkan/vulkan.h>

VulkanExportManager::VulkanExportManager() {}

VulkanExportManager::~VulkanExportManager() { Cleanup(); }

bool VulkanExportManager::Initialize(int width, int height) {
  m_Width = width;
  m_Height = height;

  std::cout << "[VulkanExportManager] Initializing for " << width << "x"
            << height << std::endl;

  // Create Vulkan context
  m_VulkanContext = std::make_shared<VulkanContext>();
  if (!m_VulkanContext->Initialize()) {
    std::cerr << "[VulkanExportManager] Failed to initialize Vulkan context"
              << std::endl;
    return false;
  }

  // Create compute pipeline
  m_ComputePipeline = std::make_shared<VulkanComputePipeline>(m_VulkanContext);
  if (!m_ComputePipeline->Initialize()) {
    std::cerr << "[VulkanExportManager] Failed to initialize compute pipeline"
              << std::endl;
    return false;
  }

  // Create command pool FIRST (needed for layout transitions in
  // CreateVulkanImages)
  if (!CreateCommandPool()) {
    std::cerr << "[VulkanExportManager] Failed to create command pool"
              << std::endl;
    return false;
  }

  // Create Vulkan images
  if (!CreateVulkanImages()) {
    std::cerr << "[VulkanExportManager] Failed to create Vulkan images"
              << std::endl;
    return false;
  }

  m_Initialized = true;
  std::cout << "[VulkanExportManager] Initialized successfully" << std::endl;
  return true;
}

bool VulkanExportManager::ConvertRGBToNV12(const uint8_t *rgbData,
                                           uint8_t *yPlane, uint8_t *uvPlane,
                                           int width, int height) {
  if (!m_Initialized) {
    std::cerr << "[VulkanExportManager] Not initialized" << std::endl;
    return false;
  }

  if (width != m_Width || height != m_Height) {
    std::cerr << "[VulkanExportManager] Dimension mismatch" << std::endl;
    return false;
  }

  // Track conversion count
  static int frameCount = 0;
  frameCount++;

  // Log every 30 frames to confirm Vulkan is active
  if (frameCount % 30 == 1) {
    std::cout << "[Vulkan] GPU conversion active (frame " << frameCount << ")"
              << std::endl;
  }

  // Upload RGB data to GPU
  size_t rgbSize = width * height * 4; // RGBA
  if (!UploadToVulkanImage(rgbData, rgbSize)) {
    std::cerr << "[VulkanExportManager] Failed to upload RGB data" << std::endl;
    return false;
  }

  // Dispatch compute shader
  if (!m_ComputePipeline->Dispatch(m_RGBInputView, m_YOutputView,
                                   m_UVOutputView, width, height)) {
    std::cerr << "[VulkanExportManager] Compute dispatch failed" << std::endl;
    return false;
  }

  // Download NV12 result from GPU
  if (!DownloadNV12FromGPU(yPlane, uvPlane)) {
    std::cerr << "[VulkanExportManager] Failed to download NV12 data"
              << std::endl;
    return false;
  }

  return true;
}

void VulkanExportManager::Cleanup() {
  if (!m_VulkanContext)
    return;

  DestroyVulkanResources();
  m_ComputePipeline.reset();
  m_VulkanContext.reset();

  m_Initialized = false;
}

bool VulkanExportManager::CreateVulkanImages() {
  if (!m_VulkanContext || !m_VulkanContext->GetDevice()) {
    return false;
  }

  VkDevice device = m_VulkanContext->GetDevice();
  uint32_t width = m_Width;
  uint32_t height = m_Height;

  // Create RGB Input Image (RGBA8)
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage =
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  if (vkCreateImage(device, &imageInfo, nullptr, &m_RGBInputImage) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to create RGB input image"
              << std::endl;
    return false;
  }

  // Create Y Output Image (R8UI)
  imageInfo.format = VK_FORMAT_R8_UINT;
  imageInfo.usage =
      VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  if (vkCreateImage(device, &imageInfo, nullptr, &m_YOutputImage) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to create Y output image"
              << std::endl;
    return false;
  }

  // Create UV Output Image (RG8UI, half resolution)
  imageInfo.format = VK_FORMAT_R8G8_UINT;
  imageInfo.extent = {width / 2, height / 2, 1};
  if (vkCreateImage(device, &imageInfo, nullptr, &m_UVOutputImage) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to create UV output image"
              << std::endl;
    return false;
  }

  // Allocate memory for all images
  VkMemoryRequirements memReqs;
  vkGetImageMemoryRequirements(device, m_RGBInputImage, &memReqs);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize =
      memReqs.size * 3; // Rough estimate for all 3 images
  allocInfo.memoryTypeIndex = m_VulkanContext->FindMemoryType(
      memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(device, &allocInfo, nullptr, &m_ImageMemory) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to allocate image memory"
              << std::endl;
    return false;
  }

  // Bind images to memory
  vkBindImageMemory(device, m_RGBInputImage, m_ImageMemory, 0);
  vkBindImageMemory(device, m_YOutputImage, m_ImageMemory, memReqs.size);
  vkBindImageMemory(device, m_UVOutputImage, m_ImageMemory, memReqs.size * 2);

  // Create Image Views
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  viewInfo.image = m_RGBInputImage;
  viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  if (vkCreateImageView(device, &viewInfo, nullptr, &m_RGBInputView) !=
      VK_SUCCESS) {
    return false;
  }

  viewInfo.image = m_YOutputImage;
  viewInfo.format = VK_FORMAT_R8_UINT;
  if (vkCreateImageView(device, &viewInfo, nullptr, &m_YOutputView) !=
      VK_SUCCESS) {
    return false;
  }

  viewInfo.image = m_UVOutputImage;
  viewInfo.format = VK_FORMAT_R8G8_UINT;
  if (vkCreateImageView(device, &viewInfo, nullptr, &m_UVOutputView) !=
      VK_SUCCESS) {
    return false;
  }

  // Create staging buffer
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = width * height * 4; // RGBA
  bufferInfo.usage =
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(device, &bufferInfo, nullptr, &m_StagingBuffer) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to create staging buffer"
              << std::endl;
    return false;
  }

  VkMemoryRequirements bufferMemReqs;
  vkGetBufferMemoryRequirements(device, m_StagingBuffer, &bufferMemReqs);

  VkMemoryAllocateInfo bufferAllocInfo{};
  bufferAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  bufferAllocInfo.allocationSize = bufferMemReqs.size;
  bufferAllocInfo.memoryTypeIndex = m_VulkanContext->FindMemoryType(
      bufferMemReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

  if (vkAllocateMemory(device, &bufferAllocInfo, nullptr, &m_StagingMemory) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to allocate staging memory"
              << std::endl;
    return false;
  }

  vkBindBufferMemory(device, m_StagingBuffer, m_StagingMemory, 0);

  // Transition output images to GENERAL layout (required for compute shader
  // writes)
  TransitionImageLayout(m_YOutputImage, VK_FORMAT_R8_UINT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);
  TransitionImageLayout(m_UVOutputImage, VK_FORMAT_R8G8_UINT,
                        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

  std::cout << "[VulkanExportManager] Vulkan images created successfully"
            << std::endl;
  return true;
}

bool VulkanExportManager::UploadToVulkanImage(const uint8_t *data,
                                              size_t size) {
  if (!m_VulkanContext || !data)
    return false;

  VkDevice device = m_VulkanContext->GetDevice();

  // Map staging buffer
  void *mapped;
  size_t rgbaSize = m_Width * m_Height * 4; // RGBA size
  if (vkMapMemory(device, m_StagingMemory, 0, rgbaSize, 0, &mapped) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to map staging memory"
              << std::endl;
    return false;
  }

  // Convert RGB24 to RGBA8 (add alpha channel = 255)
  uint8_t *dst = static_cast<uint8_t *>(mapped);
  const uint8_t *src = data;

  for (uint32_t i = 0; i < m_Width * m_Height; ++i) {
    dst[i * 4 + 0] = src[i * 3 + 0]; // R
    dst[i * 4 + 1] = src[i * 3 + 1]; // G
    dst[i * 4 + 2] = src[i * 3 + 2]; // B
    dst[i * 4 + 3] = 255;            // A (opaque)
  }

  vkUnmapMemory(device, m_StagingMemory);

  // Transition image to TRANSFER_DST layout
  TransitionImageLayout(m_RGBInputImage, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  // Copy buffer to image
  VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;
  region.imageOffset = {0, 0, 0};
  region.imageExtent = {static_cast<uint32_t>(m_Width),
                        static_cast<uint32_t>(m_Height), 1};

  vkCmdCopyBufferToImage(commandBuffer, m_StagingBuffer, m_RGBInputImage,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  EndSingleTimeCommands(commandBuffer);

  // Transition image to GENERAL layout for compute shader
  TransitionImageLayout(m_RGBInputImage, VK_FORMAT_R8G8B8A8_UNORM,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL);

  return true;
}

bool VulkanExportManager::DownloadNV12FromGPU(uint8_t *y_plane,
                                              uint8_t *uv_plane) {
  if (!m_VulkanContext || !y_plane || !uv_plane)
    return false;

  VkDevice device = m_VulkanContext->GetDevice();

  // Transition Y output to TRANSFER_SRC layout
  TransitionImageLayout(m_YOutputImage, VK_FORMAT_R8_UINT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  // Transition UV output to TRANSFER_SRC layout
  TransitionImageLayout(m_UVOutputImage, VK_FORMAT_R8G8_UINT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

  // Copy Y plane to staging buffer
  VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

  VkBufferImageCopy yRegion{};
  yRegion.bufferOffset = 0;
  yRegion.bufferRowLength = 0;
  yRegion.bufferImageHeight = 0;
  yRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  yRegion.imageSubresource.mipLevel = 0;
  yRegion.imageSubresource.baseArrayLayer = 0;
  yRegion.imageSubresource.layerCount = 1;
  yRegion.imageOffset = {0, 0, 0};
  yRegion.imageExtent = {static_cast<uint32_t>(m_Width),
                         static_cast<uint32_t>(m_Height), 1};

  vkCmdCopyImageToBuffer(commandBuffer, m_YOutputImage,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_StagingBuffer,
                         1, &yRegion);

  EndSingleTimeCommands(commandBuffer);

  // Map and copy Y data
  void *mapped;
  size_t ySize = m_Width * m_Height;
  if (vkMapMemory(device, m_StagingMemory, 0, ySize, 0, &mapped) ==
      VK_SUCCESS) {
    std::memcpy(y_plane, mapped, ySize);
    vkUnmapMemory(device, m_StagingMemory);
  } else {
    std::cerr << "[VulkanExportManager] Failed to map Y plane" << std::endl;
    return false;
  }

  // Copy UV plane
  commandBuffer = BeginSingleTimeCommands();

  VkBufferImageCopy uvRegion{};
  uvRegion.bufferOffset = 0;
  uvRegion.bufferRowLength = 0;
  uvRegion.bufferImageHeight = 0;
  uvRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  uvRegion.imageSubresource.mipLevel = 0;
  uvRegion.imageSubresource.baseArrayLayer = 0;
  uvRegion.imageSubresource.layerCount = 1;
  uvRegion.imageOffset = {0, 0, 0};
  uvRegion.imageExtent = {static_cast<uint32_t>(m_Width / 2),
                          static_cast<uint32_t>(m_Height / 2), 1};

  vkCmdCopyImageToBuffer(commandBuffer, m_UVOutputImage,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_StagingBuffer,
                         1, &uvRegion);

  EndSingleTimeCommands(commandBuffer);

  // Transition back to GENERAL for next compute shader dispatch
  TransitionImageLayout(m_YOutputImage, VK_FORMAT_R8_UINT,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL);
  TransitionImageLayout(m_UVOutputImage, VK_FORMAT_R8G8_UINT,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                        VK_IMAGE_LAYOUT_GENERAL);

  // Map and copy UV data
  size_t uvSize = (m_Width / 2) * (m_Height / 2) * 2; // RG8
  if (vkMapMemory(device, m_StagingMemory, 0, uvSize, 0, &mapped) ==
      VK_SUCCESS) {
    std::memcpy(uv_plane, mapped, uvSize);
    vkUnmapMemory(device, m_StagingMemory);
  } else {
    std::cerr << "[VulkanExportManager] Failed to map UV plane" << std::endl;
    return false;
  }

  return true;
}

void VulkanExportManager::DestroyVulkanResources() {
  if (!m_VulkanContext)
    return;

  VkDevice device = m_VulkanContext->GetDevice();

  if (m_RGBInputView)
    vkDestroyImageView(device, m_RGBInputView, nullptr);
  if (m_YOutputView)
    vkDestroyImageView(device, m_YOutputView, nullptr);
  if (m_UVOutputView)
    vkDestroyImageView(device, m_UVOutputView, nullptr);

  if (m_RGBInputImage)
    vkDestroyImage(device, m_RGBInputImage, nullptr);
  if (m_YOutputImage)
    vkDestroyImage(device, m_YOutputImage, nullptr);
  if (m_UVOutputImage)
    vkDestroyImage(device, m_UVOutputImage, nullptr);

  if (m_ImageMemory)
    vkFreeMemory(device, m_ImageMemory, nullptr);

  if (m_StagingBuffer)
    vkDestroyBuffer(device, m_StagingBuffer, nullptr);
  if (m_StagingMemory)
    vkFreeMemory(device, m_StagingMemory, nullptr);

  if (m_CommandPool)
    vkDestroyCommandPool(device, m_CommandPool, nullptr);

  std::cout << "[VulkanExportManager] Resources destroyed" << std::endl;
}

// ============================================================================
// Command Buffer Helpers
// ============================================================================

bool VulkanExportManager::CreateCommandPool() {
  VkDevice device = m_VulkanContext->GetDevice();
  uint32_t computeQueueFamily = m_VulkanContext->GetComputeQueueFamily();

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = computeQueueFamily;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_CommandPool) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to create command pool"
              << std::endl;
    return false;
  }

  // Allocate persistent command buffer
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(device, &allocInfo, &m_CommandBuffer) !=
      VK_SUCCESS) {
    std::cerr << "[VulkanExportManager] Failed to allocate command buffer"
              << std::endl;
    return false;
  }

  return true;
}

VkCommandBuffer VulkanExportManager::BeginSingleTimeCommands() {
  VkCommandBuffer commandBuffer = m_CommandBuffer;

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkResetCommandBuffer(commandBuffer, 0);
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  return commandBuffer;
}

void VulkanExportManager::EndSingleTimeCommands(VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  VkQueue computeQueue = m_VulkanContext->GetComputeQueue();
  vkQueueSubmit(computeQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(computeQueue);
}

void VulkanExportManager::TransitionImageLayout(VkImage image, VkFormat format,
                                                VkImageLayout oldLayout,
                                                VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = BeginSingleTimeCommands();

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags sourceStage;
  VkPipelineStageFlags destinationStage;

  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    destinationStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    sourceStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = 0;
    sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    destinationStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
  }

  vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  EndSingleTimeCommands(commandBuffer);
}

#endif // USE_VULKAN
