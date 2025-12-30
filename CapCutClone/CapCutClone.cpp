#include "Application.h"
#include <iostream>

int main() {
    try {
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
    }
    catch (const std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return -1;
    }
}
