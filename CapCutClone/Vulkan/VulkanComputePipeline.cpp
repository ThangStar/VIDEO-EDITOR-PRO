#ifdef USE_VULKAN

#include "VulkanComputePipeline.h"
#include <fstream>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

// Helper to get executable directory
static std::string GetExecutableDir() {
#ifdef _WIN32
  char path[MAX_PATH];
  GetModuleFileNameA(NULL, path, MAX_PATH);
  std::string fullPath(path);
  size_t pos = fullPath.find_last_of("\\/");
  return fullPath.substr(0, pos + 1);
#else
  return "./";
#endif
}

// Helper to read file
static std::vector<char> ReadFile(const std::string &filename) {
  // Try executable directory first
  std::string exeDir = GetExecutableDir();
  std::string fullPath = exeDir + filename;

  std::ifstream file(fullPath, std::ios::ate | std::ios::binary);

  // Fallback to current directory
  if (!file.is_open()) {
    file.open(filename, std::ios::ate | std::ios::binary);
  }

  if (!file.is_open()) {
    std::cerr << "Failed to read shader file: " << filename << std::endl;
    std::cerr << "Tried: " << fullPath << " and " << filename << std::endl;
    return {};
  }

  size_t fileSize = (size_t)file.tellg();
  std::vector<char> buffer(fileSize);

  file.seekg(0);
  file.read(buffer.data(), fileSize);
  file.close();

  std::cout << "[VulkanComputePipeline] Loaded shader: " << filename << " ("
            << fileSize << " bytes)" << std::endl;
  return buffer;
}

struct PushConstants {
  int width;
  int height;
};

VulkanComputePipeline::VulkanComputePipeline(
    std::shared_ptr<VulkanContext> context)
    : m_Context(context) {}

VulkanComputePipeline::~VulkanComputePipeline() { Cleanup(); }

bool VulkanComputePipeline::Initialize() {
  std::cout << "[VulkanComputePipeline] Initializing..." << std::endl;

  if (!CreateDescriptorSetLayout()) {
    return false;
  }

  if (!CreatePipeline()) {
    return false;
  }

  if (!CreateDescriptorPool()) {
    return false;
  }

  // Command infrastructure for dispatch (if separate from render pipeline)
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = m_Context->GetComputeQueueFamily();
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(m_Context->GetDevice(), &poolInfo, nullptr,
                          &m_CommandPool) != VK_SUCCESS) {
    std::cerr << "[VulkanComputePipeline] Failed to create command pool"
              << std::endl;
    return false;
  }

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = m_CommandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  if (vkAllocateCommandBuffers(m_Context->GetDevice(), &allocInfo,
                               &m_CommandBuffer) != VK_SUCCESS) {
    std::cerr << "[VulkanComputePipeline] Failed to allocate command buffer"
              << std::endl;
    return false;
  }

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  if (vkCreateFence(m_Context->GetDevice(), &fenceInfo, nullptr, &m_Fence) !=
      VK_SUCCESS) {
    return false;
  }

  m_Initialized = true;
  std::cout << "[VulkanComputePipeline] Initialized successfully" << std::endl;
  return true;
}

void VulkanComputePipeline::Cleanup() {
  VkDevice device = m_Context->GetDevice();

  if (m_Fence != VK_NULL_HANDLE) {
    vkDestroyFence(device, m_Fence, nullptr);
    m_Fence = VK_NULL_HANDLE;
  }

  if (m_CommandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, m_CommandPool, nullptr);
    m_CommandPool = VK_NULL_HANDLE;
  }

  if (m_DescriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
    m_DescriptorPool = VK_NULL_HANDLE;
  }

  if (m_ComputePipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, m_ComputePipeline, nullptr);
    m_ComputePipeline = VK_NULL_HANDLE;
  }

  if (m_PipelineLayout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, m_PipelineLayout, nullptr);
    m_PipelineLayout = VK_NULL_HANDLE;
  }

  if (m_DescriptorSetLayout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, m_DescriptorSetLayout, nullptr);
    m_DescriptorSetLayout = VK_NULL_HANDLE;
  }

  m_Initialized = false;
}

VkShaderModule VulkanComputePipeline::CreateShaderModule(const uint32_t *code,
                                                         size_t size) {
  VkShaderModuleCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  createInfo.codeSize = size;
  createInfo.pCode = code;

  VkShaderModule shaderModule;
  if (vkCreateShaderModule(m_Context->GetDevice(), &createInfo, nullptr,
                           &shaderModule) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }

  return shaderModule;
}

bool VulkanComputePipeline::CreateDescriptorSetLayout() {
  VkDescriptorSetLayoutBinding bindings[3];

  // Binding 0: Input Image (RGB)
  bindings[0].binding = 0;
  bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  bindings[0].descriptorCount = 1;
  bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  bindings[0].pImmutableSamplers = nullptr;

  // Binding 1: Y Output Image
  bindings[1].binding = 1;
  bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  bindings[1].descriptorCount = 1;
  bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  bindings[1].pImmutableSamplers = nullptr;

  // Binding 2: UV Output Image
  bindings[2].binding = 2;
  bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  bindings[2].descriptorCount = 1;
  bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  bindings[2].pImmutableSamplers = nullptr;

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = 3;
  layoutInfo.pBindings = bindings;

  if (vkCreateDescriptorSetLayout(m_Context->GetDevice(), &layoutInfo, nullptr,
                                  &m_DescriptorSetLayout) != VK_SUCCESS) {
    std::cerr
        << "[VulkanComputePipeline] Failed to create descriptor set layout"
        << std::endl;
    return false;
  }

  return true;
}

bool VulkanComputePipeline::CreatePipeline() {
  // Load shader from binary file
  auto shaderCode = ReadFile("RGB_to_NV12.spv");
  if (shaderCode.empty()) {
    std::cerr << "Failed to read shader file: RGB_to_NV12.spv" << std::endl;
    return false;
  }

  VkShaderModule computeShaderModule = CreateShaderModule(
      reinterpret_cast<const uint32_t *>(shaderCode.data()), shaderCode.size());

  if (computeShaderModule == VK_NULL_HANDLE) {
    std::cerr << "[VulkanComputePipeline] Failed to report shader module"
              << std::endl;
    return false;
  }

  VkPipelineShaderStageCreateInfo shaderStageInfo{};
  shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  shaderStageInfo.module = computeShaderModule;
  shaderStageInfo.pName = "main";

  // Push Constants
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstants);

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &m_DescriptorSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

  if (vkCreatePipelineLayout(m_Context->GetDevice(), &pipelineLayoutInfo,
                             nullptr, &m_PipelineLayout) != VK_SUCCESS) {
    vkDestroyShaderModule(m_Context->GetDevice(), computeShaderModule, nullptr);
    return false;
  }

  VkComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.layout = m_PipelineLayout;
  pipelineInfo.stage = shaderStageInfo;

  if (vkCreateComputePipelines(m_Context->GetDevice(), VK_NULL_HANDLE, 1,
                               &pipelineInfo, nullptr,
                               &m_ComputePipeline) != VK_SUCCESS) {
    vkDestroyShaderModule(m_Context->GetDevice(), computeShaderModule, nullptr);
    return false;
  }

  vkDestroyShaderModule(m_Context->GetDevice(), computeShaderModule, nullptr);
  return true;
}

bool VulkanComputePipeline::CreateDescriptorPool() {
  VkDescriptorPoolSize poolSizes[1];
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  poolSizes[0].descriptorCount = 30; // 3 bindings * 10 sets

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.poolSizeCount = 1;
  poolInfo.pPoolSizes = poolSizes;
  poolInfo.maxSets = 10;

  if (vkCreateDescriptorPool(m_Context->GetDevice(), &poolInfo, nullptr,
                             &m_DescriptorPool) != VK_SUCCESS) {
    return false;
  }
  return true;
}

bool VulkanComputePipeline::Dispatch(VkImageView inputImage, VkImageView yImage,
                                     VkImageView uvImage, int width,
                                     int height) {
  if (!m_Initialized)
    return false;

  // Update Descriptor Set
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = m_DescriptorPool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &m_DescriptorSetLayout;

  VkDescriptorSet descriptorSet;
  if (vkAllocateDescriptorSets(m_Context->GetDevice(), &allocInfo,
                               &descriptorSet) != VK_SUCCESS) {
    std::cerr << "Failed to allocate descriptor set" << std::endl;
    return false;
  }

  // Update descriptors
  VkDescriptorImageInfo inputInfo{};
  inputInfo.imageView = inputImage;
  inputInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkDescriptorImageInfo yInfo{};
  yInfo.imageView = yImage;
  yInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkDescriptorImageInfo uvInfo{};
  uvInfo.imageView = uvImage;
  uvInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  VkWriteDescriptorSet descriptorWrites[3]{};

  descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[0].dstSet = descriptorSet;
  descriptorWrites[0].dstBinding = 0;
  descriptorWrites[0].dstArrayElement = 0;
  descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  descriptorWrites[0].descriptorCount = 1;
  descriptorWrites[0].pImageInfo = &inputInfo;

  descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[1].dstSet = descriptorSet;
  descriptorWrites[1].dstBinding = 1;
  descriptorWrites[1].dstArrayElement = 0;
  descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  descriptorWrites[1].descriptorCount = 1;
  descriptorWrites[1].pImageInfo = &yInfo;

  descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  descriptorWrites[2].dstSet = descriptorSet;
  descriptorWrites[2].dstBinding = 2;
  descriptorWrites[2].dstArrayElement = 0;
  descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  descriptorWrites[2].descriptorCount = 1;
  descriptorWrites[2].pImageInfo = &uvInfo;

  vkUpdateDescriptorSets(m_Context->GetDevice(), 3, descriptorWrites, 0,
                         nullptr);

  // Record Command Buffer
  vkResetFences(m_Context->GetDevice(), 1, &m_Fence);
  vkResetCommandBuffer(m_CommandBuffer, 0);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(m_CommandBuffer, &beginInfo);

  vkCmdBindPipeline(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                    m_ComputePipeline);

  vkCmdBindDescriptorSets(m_CommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                          m_PipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

  PushConstants constants;
  constants.width = width;
  constants.height = height;
  vkCmdPushConstants(m_CommandBuffer, m_PipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants),
                     &constants);

  // Group size is 16x16
  vkCmdDispatch(m_CommandBuffer, (width + 15) / 16, (height + 15) / 16, 1);

  vkEndCommandBuffer(m_CommandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &m_CommandBuffer;

  VkResult result =
      vkQueueSubmit(m_Context->GetComputeQueue(), 1, &submitInfo, m_Fence);
  if (result != VK_SUCCESS) {
    std::cerr << "Compute queue submit failed" << std::endl;
    // Should free set
    return false;
  }

  // Wait for completion (Sync for now)
  vkWaitForFences(m_Context->GetDevice(), 1, &m_Fence, VK_TRUE, UINT64_MAX);

  // Cleanup set (Normally we reuse)
  vkFreeDescriptorSets(m_Context->GetDevice(), m_DescriptorPool, 1,
                       &descriptorSet);

  return true;
}

#endif // USE_VULKAN
