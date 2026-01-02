#pragma once

#ifdef USE_VULKAN

#include <cstdint>
#include <memory>

// Forward declarations
class VulkanContext;
class VulkanComputePipeline;
typedef struct VkImage_T *VkImage;
typedef struct VkImageView_T *VkImageView;
typedef struct VkDeviceMemory_T *VkDeviceMemory;
typedef struct VkBuffer_T *VkBuffer;
typedef struct VkCommandPool_T *VkCommandPool;
typedef struct VkCommandBuffer_T *VkCommandBuffer;
typedef enum VkFormat VkFormat;
typedef enum VkImageLayout VkImageLayout;

/**
 * @brief Vulkan-based export manager for RGB→NV12 conversion
 *
 * Standalone class that uses Vulkan compute shaders for color space conversion.
 * Can be used alongside or instead of CUDA-based conversion in
 * HardwareExportManager.
 */
class VulkanExportManager {
public:
  VulkanExportManager();
  ~VulkanExportManager();

  // Initialize Vulkan context and compute pipeline
  bool Initialize(int width, int height);

  // Process RGB frame to NV12
  bool ConvertRGBToNV12(
      const uint8_t *rgbData, // Input: RGB data (width * height * 3)
      uint8_t *yPlane,        // Output: Y plane (width * height)
      uint8_t *uvPlane,       // Output: UV plane (width * height / 2)
      int width, int height);

  // Cleanup
  void Cleanup();

  bool IsInitialized() const { return m_Initialized; }

private:
  // Vulkan resources
  std::shared_ptr<VulkanContext> m_VulkanContext;
  std::shared_ptr<VulkanComputePipeline> m_ComputePipeline;

  // Vulkan Images for compute shader
  VkImage m_RGBInputImage = nullptr;
  VkImage m_YOutputImage = nullptr;
  VkImage m_UVOutputImage = nullptr;
  VkDeviceMemory m_ImageMemory = nullptr;

  // Image Views for descriptor binding
  VkImageView m_RGBInputView = nullptr;
  VkImageView m_YOutputView = nullptr;
  VkImageView m_UVOutputView = nullptr;

  // Staging buffers for CPU↔GPU transfer
  VkBuffer m_StagingBuffer = nullptr;
  VkDeviceMemory m_StagingMemory = nullptr;

  // Command pool and buffer for transfer operations
  VkCommandPool m_CommandPool = nullptr;
  VkCommandBuffer m_CommandBuffer = nullptr;

  // Dimensions
  int m_Width = 0;
  int m_Height = 0;
  bool m_Initialized = false;

  // Helper functions
  bool CreateVulkanImages();
  bool CreateCommandPool();
  VkCommandBuffer BeginSingleTimeCommands();
  void EndSingleTimeCommands(VkCommandBuffer commandBuffer);
  void TransitionImageLayout(VkImage image, VkFormat format,
                             VkImageLayout oldLayout, VkImageLayout newLayout);
  bool UploadToVulkanImage(const uint8_t *data, size_t size);
  bool DownloadNV12FromGPU(uint8_t *y_plane, uint8_t *uv_plane);
  void DestroyVulkanResources();
};

#endif // USE_VULKAN
