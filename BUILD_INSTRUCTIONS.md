# Phase 1 Build Instructions

## Prerequisites Check

Before building, ensure you have installed all required libraries. The error shows that **ImGui is not installed yet**.

## Install ImGui

Run these commands from your vcpkg directory:

```powershell
# Navigate to vcpkg directory
cd D:\ts-ws\learn_cpp\vcpkg

# Install ImGui with all backends
.\vcpkg install imgui[core,glfw-binding,opengl3-binding]:x64-windows
```

> **Note**: The `[core,glfw-binding,opengl3-binding]` features are required for the GLFW and OpenGL3 backends that we use in the application.

## Alternative: Install Base ImGui

If the above command fails, try installing just the base ImGui package:

```powershell
.\vcpkg install imgui:x64-windows
```

## Build the Application

### Option 1: Using Visual Studio

1. Open Visual Studio
2. Open the folder `d:\ts-ws\CapCutClone`
3. VS will automatically detect CMakeLists.txt
4. Build → Build All (Ctrl+Shift+B)

### Option 2: Using CMake Command Line

```powershell
# Navigate to project root
cd d:\ts-ws\CapCutClone

# Create build directory
mkdir build -Force
cd build

# Configure
cmake .. -DCMAKE_TOOLCHAIN_FILE="D:/ts-ws/learn_cpp/vcpkg/scripts/buildsystems/vcpkg.cmake"

# Build
cmake --build . --config Debug
```

### Option 3: Using VSCode CMake Tools

1. Open Command Palette (Ctrl+Shift+P)
2. Run **CMake: Configure**
3. Run **CMake: Build**

## Troubleshooting

### CMake not found

If you get "cmake is not recognized", either:
- Use Visual Studio's built-in CMake
- Install CMake and add to PATH
- Use VSCode CMake Tools extension

### ImGui headers not found

Make sure vcpkg integration is active:
```powershell
.\vcpkg integrate install
```

## Next Steps

Once the build succeeds:
1. Run the application (CapCutClone.exe in build folder)
2. Verify the UI appears correctly
3. Test all panels and controls
