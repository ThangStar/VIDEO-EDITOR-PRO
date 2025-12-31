cmake_minimum_required(VERSION 3.10)
project(CapCutClone)

# Set C++ standard to 17
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages from vcpkg
find_package(glfw3 CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(imgui CONFIG REQUIRED)

# Find FFmpeg component packages (vcpkg provides them separately)
find_package(unofficial-ffmpeg CONFIG QUIET)
if(unofficial-ffmpeg_FOUND)
    message(STATUS "Found FFmpeg via unofficial-ffmpeg package")
    set(FFMPEG_LIBRARIES unofficial::ffmpeg::avformat unofficial::ffmpeg::avcodec unofficial::ffmpeg::avutil unofficial::ffmpeg::swscale)
else()
    # Fallback: Hardcoded paths for vcpkg
    message(STATUS "Using hardcoded FFmpeg paths for vcpkg")
    set(FFMPEG_INCLUDE_DIRS "D:/ts-ws/learn_cpp/vcpkg/installed/x64-windows/include")
    set(FFMPEG_LIBRARY_DIRS "D:/ts-ws/learn_cpp/vcpkg/installed/x64-windows/lib")
    set(FFMPEG_LIBRARIES avformat avcodec avutil swscale)
endif()


# Add all source files
add_executable(CapCutClone 
    CapCutClone/CapCutClone.cpp
    CapCutClone/Application.cpp
    CapCutClone/UI/UIManager.cpp
    CapCutClone/UI/TimelineThumbnails.cpp
    CapCutClone/Video/VideoPlayer.cpp
    CapCutClone/Rendering/TextureRenderer.cpp
    CapCutClone/Timeline/TimelineManager.cpp
    CapCutClone/Timeline/EffectLayer.cpp
    CapCutClone/Encoder/VideoEncoder.cpp
    CapCutClone/Configuration.cpp
    CapCutClone/Encoder/ExportManager.cpp
)

# Include directories
target_include_directories(CapCutClone PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/CapCutClone
    ${FFMPEG_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(CapCutClone PRIVATE 
    glfw 
    glad::glad
    OpenGL::GL
    imgui::imgui
)

# Link FFmpeg libraries
if(unofficial-ffmpeg_FOUND)
    target_link_libraries(CapCutClone PRIVATE ${FFMPEG_LIBRARIES})
else()
    # Add FFmpeg library directories if needed
    if(FFMPEG_LIBRARY_DIRS)
        target_link_directories(CapCutClone PRIVATE ${FFMPEG_LIBRARY_DIRS})
    endif()
    # Link FFmpeg libraries in correct dependency order
    # avutil must come last as other libraries depend on it
    target_link_libraries(CapCutClone PRIVATE 
        avformat
        avcodec
        swscale
        avutil
        avutil  # Link twice to resolve circular dependencies
    )
endif()