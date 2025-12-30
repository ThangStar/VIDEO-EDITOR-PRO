#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>

// Forward declarations
class UIManager;
class VideoPlayer;
class TextureRenderer;

class Application {
public:
    Application(int width = 1280, int height = 720, const std::string& title = "CapCut Clone - Video Editor");
    ~Application();

    // Main application methods
    bool Initialize();
    void Run();
    void Shutdown();

    // Getters
    GLFWwindow* GetWindow() const { return m_Window; }
    bool IsRunning() const { return m_IsRunning; }
    VideoPlayer* GetVideoPlayer() const { return m_VideoPlayer; }
    TextureRenderer* GetTextureRenderer() const { return m_TextureRenderer; }

    // File operations
    void OpenVideoFile();

private:
    // Window management
    GLFWwindow* m_Window;
    int m_Width;
    int m_Height;
    std::string m_Title;
    bool m_IsRunning;

    // Subsystems
    UIManager* m_UIManager;
    VideoPlayer* m_VideoPlayer;
    TextureRenderer* m_TextureRenderer;

    // Internal methods
    bool InitializeGLFW();
    bool InitializeGLAD();
    bool InitializeImGui();
    void ProcessInput();
    void Update(float deltaTime);
    void Render();
    void CleanupImGui();

    // GLFW callbacks
    static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);
    static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
};
