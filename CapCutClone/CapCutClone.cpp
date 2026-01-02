#include "Application.h"
#include <iostream>

#ifdef USE_VULKAN
#include "Vulkan/VulkanTest.h"
#endif

int main(int argc, char *argv[]) {
  try {
#ifdef USE_VULKAN
    if (argc > 1 && std::string(argv[1]) == "--test-vulkan") {
      std::cout << "\n[Main] Running Vulkan Test\n" << std::endl;
      return VulkanTest::TestRGBToNV12Conversion() ? 0 : 1;
    }
#endif
    // Create application instance
    Application app(1280, 720, "CapCut Clone - Video Editor");

    // Initialize
    if (!app.Initialize()) {
      std::cerr << "Failed to initialize application!" << std::endl;
      return -1;
    }

    // Run main loop
    app.Run();

    // Cleanup happens in destructor
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return -1;
  }
}
