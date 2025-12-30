#include "Application.h"
#include "UI/UIManager.h"
#include "Video/VideoPlayer.h"
#include "Rendering/TextureRenderer.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <iostream>

// Windows file dialog
#ifdef _WIN32
#include <windows.h>
#endif

// Temporary global pointer for UI to access Application (will be replaced with event system)
Application* g_Application = nullptr;

Application::Application(int width, int height, const std::string& title)
    : m_Window(nullptr)
    , m_Width(width)
    , m_Height(height)
    , m_Title(title)
    , m_IsRunning(false)
    , m_UIManager(nullptr)
{
}

Application::~Application() {
    Shutdown();
}

bool Application::Initialize() {
    // Initialize GLFW
    if (!InitializeGLFW()) {
        std::cout << "Failed to initialize GLFW!" << std::endl;
        return false;
    }

    // Initialize GLAD
    if (!InitializeGLAD()) {
        std::cout << "Failed to initialize GLAD!" << std::endl;
        return false;
    }

    // Initialize ImGui
    if (!InitializeImGui()) {
        std::cout << "Failed to initialize ImGui!" << std::endl;
        return false;
    }

    // Create subsystems
    m_VideoPlayer = new VideoPlayer();
    m_TextureRenderer = new TextureRenderer();
    
    if (!m_TextureRenderer->Initialize()) {
        std::cout << "Failed to initialize TextureRenderer!" << std::endl;
        return false;
    }
    
    // Create UI Manager and pass subsystems
    m_UIManager = new UIManager();
    m_UIManager->SetVideoPlayer(m_VideoPlayer);
    m_UIManager->SetTextureRenderer(m_TextureRenderer);

    // Set global pointer for UI callbacks
    g_Application = this;

    m_IsRunning = true;
    std::cout << "Application initialized successfully!" << std::endl;
    return true;
}

void Application::Run() {
    if (!m_IsRunning) {
        std::cout << "Application is not initialized!" << std::endl;
        return;
    }

    float lastFrame = 0.0f;

    // Main loop
    while (!glfwWindowShouldClose(m_Window) && m_IsRunning) {
        // Calculate delta time
        float currentFrame = static_cast<float>(glfwGetTime());
        float deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Process input
        ProcessInput();

        // Update
        Update(deltaTime);

        // Render
        Render();

        // Swap buffers and poll events
        glfwSwapBuffers(m_Window);
        glfwPollEvents();
    }
}

void Application::Shutdown() {
    // Clear global pointer
    g_Application = nullptr;

    if (m_UIManager) {
        delete m_UIManager;
        m_UIManager = nullptr;
    }

    if (m_TextureRenderer) {
        delete m_TextureRenderer;
        m_TextureRenderer = nullptr;
    }

    if (m_VideoPlayer) {
        delete m_VideoPlayer;
        m_VideoPlayer = nullptr;
    }

    CleanupImGui();

    if (m_Window) {
        glfwDestroyWindow(m_Window);
        m_Window = nullptr;
    }

    glfwTerminate();
    m_IsRunning = false;
}

bool Application::InitializeGLFW() {
    if (!glfwInit()) {
        return false;
    }

    // Set OpenGL version (3.3 Core)
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Create window
    m_Window = glfwCreateWindow(m_Width, m_Height, m_Title.c_str(), nullptr, nullptr);
    if (!m_Window) {
        glfwTerminate();
        return false;
    }

    glfwMakeContextCurrent(m_Window);
    glfwSwapInterval(1); // Enable vsync

    // Set callbacks
    glfwSetWindowUserPointer(m_Window, this);
    glfwSetFramebufferSizeCallback(m_Window, FramebufferSizeCallback);
    glfwSetKeyCallback(m_Window, KeyCallback);

    return true;
}

bool Application::InitializeGLAD() {
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        return false;
    }

    // Set viewport
    glViewport(0, 0, m_Width, m_Height);

    return true;
}

bool Application::InitializeImGui() {
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; 

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    
    // CapCut Theme Palette
    ImVec4 bgDark       = ImVec4(0.11f, 0.11f, 0.12f, 1.00f); // #1D1D1F
    ImVec4 bgPanel      = ImVec4(0.15f, 0.15f, 0.16f, 1.00f); // #252528
    ImVec4 bgInput      = ImVec4(0.20f, 0.20f, 0.21f, 1.00f); // #333335
    ImVec4 border       = ImVec4(0.25f, 0.25f, 0.26f, 1.00f); // #444444
    ImVec4 accent       = ImVec4(0.00f, 0.78f, 0.84f, 1.00f); // #00C8D6 (Teal/Cyan)
    ImVec4 accentHover  = ImVec4(0.00f, 0.85f, 0.90f, 1.00f);
    ImVec4 accentActive = ImVec4(0.00f, 0.65f, 0.70f, 1.00f);
    ImVec4 textMain     = ImVec4(0.95f, 0.95f, 0.95f, 1.00f);
    ImVec4 textDim      = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = textMain;
    colors[ImGuiCol_TextDisabled]           = textDim;
    colors[ImGuiCol_WindowBg]               = bgDark;
    colors[ImGuiCol_ChildBg]                = bgPanel;
    colors[ImGuiCol_PopupBg]                = bgInput;
    colors[ImGuiCol_Border]                 = border;
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
    colors[ImGuiCol_FrameBg]                = bgInput;
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.25f, 0.25f, 0.26f, 1.00f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.28f, 0.28f, 0.29f, 1.00f);
    colors[ImGuiCol_TitleBg]                = bgDark;
    colors[ImGuiCol_TitleBgActive]          = bgDark;
    colors[ImGuiCol_TitleBgCollapsed]       = bgDark;
    colors[ImGuiCol_MenuBarBg]              = bgDark;
    colors[ImGuiCol_ScrollbarBg]            = bgDark;
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]              = accent;
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.50f, 0.50f, 0.50f, 1.00f); // Circle Style?
    colors[ImGuiCol_SliderGrabActive]       = accent;
    colors[ImGuiCol_Button]                 = bgInput;
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.25f, 0.25f, 0.26f, 1.00f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.18f, 0.18f, 0.19f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.20f, 0.20f, 0.20f, 0.00f); // Transparent headers unless hovered
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.25f, 0.25f, 0.26f, 1.00f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.28f, 0.28f, 0.29f, 1.00f);
    colors[ImGuiCol_Separator]              = border;
    colors[ImGuiCol_SeparatorHovered]       = accent;
    colors[ImGuiCol_SeparatorActive]        = accent;
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripHovered]      = border;
    colors[ImGuiCol_ResizeGripActive]       = border;
    colors[ImGuiCol_Tab]                    = bgDark;
    colors[ImGuiCol_TabHovered]             = bgPanel;
    colors[ImGuiCol_TabActive]              = bgPanel;
    colors[ImGuiCol_TabUnfocused]           = bgDark;
    colors[ImGuiCol_TabUnfocusedActive]     = bgPanel;

    // Styling Metrics
    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(8.0f, 6.0f);
    style.CellPadding       = ImVec2(6.0f, 6.0f);
    style.ItemSpacing       = ImVec2(10.0f, 10.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
    style.IndentSpacing     = 20.0f;
    style.ScrollbarSize     = 10.0f;
    style.GrabMinSize       = 12.0f;

    // Rounding - Soft, modern look
    style.WindowRounding    = 8.0f;
    style.ChildRounding     = 6.0f;
    style.FrameRounding     = 6.0f;
    style.PopupRounding     = 6.0f;
    style.ScrollbarRounding = 12.0f;
    style.GrabRounding      = 6.0f;
    style.TabRounding       = 6.0f;
    
    // Thickness
    style.WindowBorderSize  = 0.0f; // Minimalist, no borders
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(m_Window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    return true;
}

void Application::ProcessInput() {
    // ESC to close
    if (glfwGetKey(m_Window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        m_IsRunning = false;
    }
}

void Application::Update(float deltaTime) {
    // Update logic will be added here in future phases
    if (m_UIManager) {
        m_UIManager->Update(deltaTime);
    }
}

void Application::Render() {
    // Clear background (dark gray, similar to CapCut)
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Start ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // Render UI
    if (m_UIManager) {
        m_UIManager->Render();
    }

    // ImGui Rendering
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Application::CleanupImGui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Application::FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
    
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (app) {
        app->m_Width = width;
        app->m_Height = height;
    }
}

void Application::KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    Application* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app || !app->m_UIManager) return;

    // Pass keyboard events to UI Manager
    if (action == GLFW_PRESS) {
        // Space for Play/Pause (will be implemented in UIManager)
        if (key == GLFW_KEY_SPACE) {
            app->m_UIManager->OnSpacePressed();
        }
    }
}

void Application::OpenVideoFile() {
#ifdef _WIN32
    OPENFILENAMEA ofn;
    char szFile[260] = {0};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Video Files\0*.mp4;*.avi;*.mov;*.mkv;*.wmv;*.flv\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = nullptr;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = nullptr;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        std::cout << "Selected file: " << szFile << std::endl;
        if (m_VideoPlayer) {
            if (m_VideoPlayer->LoadVideo(szFile)) {
                // Create texture for video
                if (m_TextureRenderer) {
                    m_TextureRenderer->CreateTexture(
                        m_VideoPlayer->GetWidth(),
                        m_VideoPlayer->GetHeight()
                    );
                }
                
                // Update UI with new video info
                if (m_UIManager) {
                    m_UIManager->OnVideoLoaded(std::string(szFile));
                }
            }
        }
    }
#else
    std::cout << "File dialog not implemented for this platform" << std::endl;
#endif
}

