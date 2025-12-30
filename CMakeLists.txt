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
    # Fallback: try pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBAV QUIET libavformat libavcodec libavutil libswscale)
        if(LIBAV_FOUND)
            message(STATUS "Found FFmpeg via pkg-config")
            set(FFMPEG_INCLUDE_DIRS ${LIBAV_INCLUDE_DIRS})
            set(FFMPEG_LIBRARIES ${LIBAV_LIBRARIES})
            set(FFMPEG_LIBRARY_DIRS ${LIBAV_LIBRARY_DIRS})
        else()
            message(FATAL_ERROR "FFmpeg not found! Please install via: vcpkg install ffmpeg:x64-windows")
        endif()
    else()
        message(FATAL_ERROR "FFmpeg not found! Please install via: vcpkg install ffmpeg:x64-windows")
    endif()
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
    CapCutClone/Encoder/VideoEncoder.cpp
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
    avformat.lib
    avcodec.lib
    swscale.lib
    avutil.lib
)

# Add FFmpeg library directories if needed
if(FFMPEG_LIBRARY_DIRS)
    target_link_directories(CapCutClone PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

