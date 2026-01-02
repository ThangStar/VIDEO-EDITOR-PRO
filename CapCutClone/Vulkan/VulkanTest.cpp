#ifdef USE_VULKAN

#include "Vulkan/VulkanExportManager.h"
#include <fstream>
#include <iostream>
#include <vector>

namespace VulkanTest {

// Generate test RGB image (gradient pattern)
void GenerateTestRGB(uint8_t *data, int width, int height) {
  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      int idx = (y * width + x) * 4;
      // Create gradient: R = x, G = y, B = 128
      data[idx + 0] = static_cast<uint8_t>((x * 255) / width);  // R
      data[idx + 1] = static_cast<uint8_t>((y * 255) / height); // G
      data[idx + 2] = 128;                                      // B
      data[idx + 3] = 255;                                      // A
    }
  }
}

// Save NV12 to file for verification
bool SaveNV12ToFile(const char *filename, const uint8_t *yPlane,
                    const uint8_t *uvPlane, int width, int height) {
  std::ofstream file(filename, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "[Test] Failed to open file: " << filename << std::endl;
    return false;
  }

  // Write Y plane
  file.write(reinterpret_cast<const char *>(yPlane), width * height);

  // Write UV plane (interleaved)
  file.write(reinterpret_cast<const char *>(uvPlane),
             (width / 2) * (height / 2) * 2);

  file.close();
  std::cout << "[Test] Saved NV12 to: " << filename << std::endl;
  return true;
}

// Main test function
bool TestRGBToNV12Conversion() {
  const int width = 1920;
  const int height = 1080;

  std::cout << "\n========================================" << std::endl;
  std::cout << "Vulkan RGB→NV12 Conversion Test" << std::endl;
  std::cout << "Resolution: " << width << "x" << height << std::endl;
  std::cout << "========================================\n" << std::endl;

  // Initialize VulkanExportManager
  VulkanExportManager manager;
  if (!manager.Initialize(width, height)) {
    std::cerr << "[Test] Failed to initialize VulkanExportManager" << std::endl;
    return false;
  }
  std::cout << "[Test] ✅ VulkanExportManager initialized" << std::endl;

  // Generate test RGB data
  std::vector<uint8_t> rgbData(width * height * 4);
  GenerateTestRGB(rgbData.data(), width, height);
  std::cout << "[Test] ✅ Generated test RGB gradient" << std::endl;

  // Allocate output buffers
  std::vector<uint8_t> yPlane(width * height);
  std::vector<uint8_t> uvPlane((width / 2) * (height / 2) * 2);

  // Perform conversion
  std::cout << "[Test] 🔄 Converting RGB→NV12..." << std::endl;
  if (!manager.ConvertRGBToNV12(rgbData.data(), yPlane.data(), uvPlane.data(),
                                width, height)) {
    std::cerr << "[Test] ❌ Conversion failed" << std::endl;
    return false;
  }
  std::cout << "[Test] ✅ Conversion completed" << std::endl;

  // Save to file for verification
  if (!SaveNV12ToFile("test_output.nv12", yPlane.data(), uvPlane.data(), width,
                      height)) {
    std::cerr << "[Test] ❌ Failed to save output" << std::endl;
    return false;
  }

  // Basic sanity check: Y plane should not be all zeros
  bool hasData = false;
  for (int i = 0; i < 100; i++) {
    if (yPlane[i] != 0) {
      hasData = true;
      break;
    }
  }

  if (!hasData) {
    std::cerr << "[Test] ⚠️  Warning: Y plane appears to be all zeros"
              << std::endl;
  }

  std::cout << "\n========================================" << std::endl;
  std::cout << "Test Summary:" << std::endl;
  std::cout << "  Y plane size: " << yPlane.size() << " bytes" << std::endl;
  std::cout << "  UV plane size: " << uvPlane.size() << " bytes" << std::endl;
  std::cout << "  Output: test_output.nv12" << std::endl;
  std::cout << "\nVerify with FFmpeg:" << std::endl;
  std::cout << "  ffplay -f rawvideo -pixel_format nv12 -video_size " << width
            << "x" << height << " test_output.nv12" << std::endl;
  std::cout << "========================================\n" << std::endl;

  manager.Cleanup();
  return true;
}

} // namespace VulkanTest

#endif // USE_VULKAN
