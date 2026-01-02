#ifdef USE_VULKAN

#include "VulkanContext.h"
#include <cstring>
#include <iostream>
#include <set>

VulkanContext::VulkanContext() {}

VulkanContext::~VulkanContext() { Cleanup(); }

bool VulkanContext::Initialize(bool enableValidation) {
  m_ValidationEnabled = enableValidation;

  std::cout << "[VulkanContext] Initializing Vulkan..." << std::endl;

  if (!CreateInstance(enableValidation)) {
    return false;
  }

  if (enableValidation) {
    SetupDebugMessenger();
  }

  if (!SelectPhysicalDevice()) {
    return false;
  }

  if (!CreateLogicalDevice()) {
    return false;
  }

  m_Initialized = true;
  std::cout << "[VulkanContext] Vulkan initialized successfully" << std::endl;
  return true;
}

void VulkanContext::Cleanup() {
  if (!m_Initialized)
    return;

  if (m_Device != VK_NULL_HANDLE) {
    vkDestroyDevice(m_Device, nullptr);
    m_Device = VK_NULL_HANDLE;
  }

  if (m_ValidationEnabled) {
    DestroyDebugMessenger();
  }

  if (m_Instance != VK_NULL_HANDLE) {
    vkDestroyInstance(m_Instance, nullptr);
    m_Instance = VK_NULL_HANDLE;
  }

  m_Initialized = false;
  std::cout << "[VulkanContext] Vulkan cleaned up" << std::endl;
}

bool VulkanContext::CreateInstance(bool enableValidation) {
  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "CapCutClone";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_2;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = GetRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  if (enableValidation && !CheckValidationLayerSupport()) {
    std::cerr << "[VulkanContext] Validation layers requested but not available"
              << std::endl;
    return false;
  }

  if (enableValidation) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(m_ValidationLayers.size());
    createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  VkResult result = vkCreateInstance(&createInfo, nullptr, &m_Instance);
  if (result != VK_SUCCESS) {
    std::cerr << "[VulkanContext] Failed to create Vulkan instance: " << result
              << std::endl;
    return false;
  }

  std::cout << "[VulkanContext] Vulkan instance created" << std::endl;
  return true;
}

std::vector<const char *> VulkanContext::GetRequiredExtensions() {
  std::vector<const char *> extensions;

  if (m_ValidationEnabled) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}

bool VulkanContext::CheckValidationLayerSupport() {
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char *layerName : m_ValidationLayers) {
    bool layerFound = false;

    for (const auto &layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

bool VulkanContext::SelectPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    std::cerr << "[VulkanContext] No GPUs with Vulkan support found"
              << std::endl;
    return false;
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());

  for (const auto &device : devices) {
    if (IsDeviceSuitable(device)) {
      m_PhysicalDevice = device;

      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(device, &props);
      std::cout << "[VulkanContext] Selected GPU: " << props.deviceName
                << std::endl;
      return true;
    }
  }

  std::cerr << "[VulkanContext] No suitable GPU found" << std::endl;
  return false;
}

bool VulkanContext::IsDeviceSuitable(VkPhysicalDevice device) {
  QueueFamilyIndices indices = FindQueueFamilies(device);
  return indices.IsComplete();
}

VulkanContext::QueueFamilyIndices
VulkanContext::FindQueueFamilies(VkPhysicalDevice device) {
  QueueFamilyIndices indices;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  int i = 0;
  for (const auto &queueFamily : queueFamilies) {
    if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
    }

    if (queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
      indices.computeFamily = i;
    }

    if (indices.IsComplete()) {
      break;
    }

    i++;
  }

  return indices;
}

bool VulkanContext::CreateLogicalDevice() {
  QueueFamilyIndices indices = FindQueueFamilies(m_PhysicalDevice);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
                                            indices.computeFamily.value()};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures{};

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(m_DeviceExtensions.size());
  createInfo.ppEnabledExtensionNames = m_DeviceExtensions.data();

  // Validation layers (deprecated for devices but keep for compatibility)
  if (m_ValidationEnabled) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(m_ValidationLayers.size());
    createInfo.ppEnabledLayerNames = m_ValidationLayers.data();
  } else {
    createInfo.enabledLayerCount = 0;
  }

  VkResult result =
      vkCreateDevice(m_PhysicalDevice, &createInfo, nullptr, &m_Device);
  if (result != VK_SUCCESS) {
    std::cerr << "[VulkanContext] Failed to create logical device: " << result
              << std::endl;
    return false;
  }

  // Get queue handles
  m_GraphicsQueueFamily = indices.graphicsFamily.value();
  m_ComputeQueueFamily = indices.computeFamily.value();

  vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
  vkGetDeviceQueue(m_Device, m_ComputeQueueFamily, 0, &m_ComputeQueue);

  std::cout << "[VulkanContext] Logical device created successfully"
            << std::endl;
  return true;
}

VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData) {

  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::cerr << "[Vulkan Validation] " << pCallbackData->pMessage << std::endl;
  }

  return VK_FALSE;
}

bool VulkanContext::SetupDebugMessenger() {
  // Debug messenger setup will be added when we link debug extension functions
  std::cout << "[VulkanContext] Debug validation enabled" << std::endl;
  return true;
}

void VulkanContext::DestroyDebugMessenger() {
  // Cleanup will match SetupDebugMessenger implementation
}

uint32_t VulkanContext::FindMemoryType(uint32_t typeFilter,
                                       VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProperties);

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  std::cerr << "[VulkanContext] Failed to find suitable memory type!"
            << std::endl;
  return 0; // Fallback
}

#endif // USE_VULKAN
