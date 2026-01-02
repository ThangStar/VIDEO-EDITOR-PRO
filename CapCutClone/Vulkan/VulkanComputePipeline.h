#pragma once

#ifdef USE_VULKAN

#include "VulkanContext.h"
#include <memory>
#include <vector>

class VulkanComputePipeline {
public:
  VulkanComputePipeline(std::shared_ptr<VulkanContext> context);
  ~VulkanComputePipeline();

  // Initialize the compute pipeline from SPIR-V bytecode
  bool Initialize();
  void Cleanup();

  // Dispatch the compute shader
  // inputImage: The source RGB image view
  // yImage: The destination Y plane image view
  // uvImage: The destination UV plane image view
  bool Dispatch(VkImageView inputImage, VkImageView yImage, VkImageView uvImage,
                int width, int height);

  bool IsInitialized() const { return m_Initialized; }

private:
  bool CreateDescriptorSetLayout();
  bool CreatePipeline();
  bool CreateDescriptorPool();
  bool CreateDescriptorSets(); // We might need dynamic sets if buffers change

  // Helper to create shader module
  VkShaderModule CreateShaderModule(const uint32_t *code, size_t size);

private:
  std::shared_ptr<VulkanContext> m_Context;

  VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
  VkPipeline m_ComputePipeline = VK_NULL_HANDLE;

  VkDescriptorSetLayout m_DescriptorSetLayout = VK_NULL_HANDLE;
  VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
  VkDescriptorSet m_DescriptorSet = VK_NULL_HANDLE; // reused set

  VkCommandPool m_CommandPool = VK_NULL_HANDLE;
  VkCommandBuffer m_CommandBuffer = VK_NULL_HANDLE;

  VkFence m_Fence = VK_NULL_HANDLE;

  bool m_Initialized = false;
};

#endif // USE_VULKAN
