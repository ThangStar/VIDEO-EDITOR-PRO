#pragma once

#ifdef USE_VULKAN

#include <optional>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>

class VulkanContext {
public:
  VulkanContext();
  ~VulkanContext();

  // Initialize Vulkan instance and device
  bool Initialize(bool enableValidation = true);
  void Cleanup();

  // Getters
  VkInstance GetInstance() const { return m_Instance; }
  VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
  VkDevice GetDevice() const { return m_Device; }
  VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
  VkQueue GetComputeQueue() const { return m_ComputeQueue; }

  uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
  uint32_t GetComputeQueueFamily() const { return m_ComputeQueueFamily; }

  // Helper to find suitable memory type
  uint32_t FindMemoryType(uint32_t typeFilter,
                          VkMemoryPropertyFlags properties) const;

  bool IsInitialized() const { return m_Initialized; }

private:
  // Instance
  bool CreateInstance(bool enableValidation);
  std::vector<const char *> GetRequiredExtensions();
  bool CheckValidationLayerSupport();

  // Physical Device
  bool SelectPhysicalDevice();
  bool IsDeviceSuitable(VkPhysicalDevice device);

  struct QueueFamilyIndices {
    std::optional<uint32_t> graphicsFamily;
    std::optional<uint32_t> computeFamily;
    bool IsComplete() {
      return graphicsFamily.has_value() && computeFamily.has_value();
    }
  };
  QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device);

  // Logical Device
  bool CreateLogicalDevice();

  // Validation
  static VKAPI_ATTR VkBool32 VKAPI_CALL
  DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                VkDebugUtilsMessageTypeFlagsEXT type,
                const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
                void *pUserData);

  bool SetupDebugMessenger();
  void DestroyDebugMessenger();

private:
  VkInstance m_Instance = VK_NULL_HANDLE;
  VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
  VkDevice m_Device = VK_NULL_HANDLE;

  VkQueue m_GraphicsQueue = VK_NULL_HANDLE;
  VkQueue m_ComputeQueue = VK_NULL_HANDLE;

  uint32_t m_GraphicsQueueFamily = 0;
  uint32_t m_ComputeQueueFamily = 0;

  VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;

  bool m_ValidationEnabled = false;
  bool m_Initialized = false;

  const std::vector<const char *> m_ValidationLayers = {
      "VK_LAYER_KHRONOS_validation"};

  const std::vector<const char *> m_DeviceExtensions = {
      // Will add VK_KHR_video_queue, etc in Phase 3
  };
};

#endif // USE_VULKAN
