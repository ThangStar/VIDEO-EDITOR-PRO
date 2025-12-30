# C++ Installation Guide for CapCutClone

This document provides step-by-step instructions for setting up your C++ development environment with all required dependencies.

## Prerequisites

### 1. Install Visual Studio with C++ Desktop Development

1. Download and install Visual Studio (Community, Professional, or Enterprise)
2. During installation, select **Desktop development with C++** workload
3. Ensure the following components are included:
   - MSVC v143 (or latest) C++ build tools
   - Windows SDK
   - C++ CMake tools for Windows

## Installing vcpkg Package Manager

### 1. Clone vcpkg Repository

```powershell
git clone https://github.com/Microsoft/vcpkg.git
```

### 2. Bootstrap vcpkg

```powershell
.\vcpkg\bootstrap-vcpkg.bat
```

### 3. Install Required Libraries

Install all dependencies using vcpkg:

```powershell
# FFmpeg - Video processing library
.\vcpkg install ffmpeg:x64-windows

# OpenGL, GLFW3, and GLAD - Graphics libraries
.\vcpkg install opengl:x64-windows glfw3:x64-windows glad:x64-windows

# OpenCV - Computer vision library
.\vcpkg install opencv:x64-windows

# ImGui - Immediate mode GUI library
.\vcpkg install imgui:x64-windows

# FreeType - Font rendering library (for text overlays)
.\vcpkg install freetype:x64-windows
```

> **Note**: Installation may take 30-60 minutes depending on your system and internet speed.

### 4. Integrate vcpkg with Visual Studio

This step allows Visual Studio to automatically find libraries installed via vcpkg:

```powershell
.\vcpkg integrate install
```

After running this command, you'll see a message with a CMake toolchain file path. Save this path for later use.

## Project Setup

### 1. Install CMake Extension for VSCode

1. Open Visual Studio Code
2. Go to Extensions (Ctrl+Shift+X)
3. Search for "CMake Tools"
4. Install the official CMake Tools extension by Microsoft

### 2. Create CMakeLists.txt

Create a `CMakeLists.txt` file in your project root with the following content:

```cmake
cmake_minimum_required(VERSION 3.20)

project(CapCutClone)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(OpenCV CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(Freetype REQUIRED)

# Add executable
add_executable(CapCutClone CapCutClone/CapCutClone.cpp)

# Link libraries
target_link_libraries(CapCutClone PRIVATE 
    glfw 
    glad::glad
    OpenGL::GL
    opencv_core
    opencv_imgproc
    opencv_videoio
    imgui::imgui
    Freetype::Freetype
)
```

### 3. Configure VSCode Settings

Add the following to your VSCode `settings.json` (File → Preferences → Settings → Open Settings (JSON)):

```json
{
    "cmake.configureSettings": {
        "CMAKE_TOOLCHAIN_FILE": "D:/ts-ws/learn_cpp/vcpkg/scripts/buildsystems/vcpkg.cmake"
    }
}
```

> **Important**: Replace the path with your actual vcpkg installation path. The path should point to `vcpkg/scripts/buildsystems/vcpkg.cmake`.

## Building the Project

### Using CMake in VSCode

1. Open the Command Palette (Ctrl+Shift+P)
2. Run **CMake: Configure**
3. Select your compiler (Visual Studio or Ninja)
4. Run **CMake: Build** to compile the project

### Using Visual Studio

1. Open the project folder in Visual Studio
2. Visual Studio will automatically detect CMakeLists.txt
3. Select your build configuration (Debug/Release)
4. Build → Build Solution (Ctrl+Shift+B)

## Troubleshooting

### Common Issues

**Issue**: CMake cannot find vcpkg libraries
- **Solution**: Verify the `CMAKE_TOOLCHAIN_FILE` path in your VSCode settings
- Ensure vcpkg integration was successful: `.\vcpkg integrate install`

**Issue**: Linker errors with GLAD
- **Solution**: Use `glad::glad` instead of just `glad` in CMakeLists.txt

**Issue**: Missing DLL files when running
- **Solution**: Copy required DLLs from vcpkg to your build output directory, or add vcpkg bin folder to PATH

## Library Versions

The following libraries will be installed:
- **FFmpeg**: Video/audio processing
- **OpenGL**: Graphics rendering API
- **GLFW3**: Window and input handling
- **GLAD**: OpenGL function loader
- **OpenCV**: Computer vision and image processing
- **ImGui**: Immediate mode graphical user interface
- **FreeType**: Font rendering for text overlays

## Additional Resources

- [vcpkg Documentation](https://vcpkg.io/)
- [CMake Documentation](https://cmake.org/documentation/)
- [GLFW Documentation](https://www.glfw.org/documentation.html)
- [OpenCV Documentation](https://docs.opencv.org/)
- [ImGui Documentation](https://github.com/ocornut/imgui)

## Next Steps

After completing the installation:
1. Verify all libraries are installed: `.\vcpkg list`
2. Test build your project using CMake
3. Start developing your CapCutClone application!
