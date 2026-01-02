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

# Vulkan is REQUIRED for hardware-accelerated video encoding
find_package(Vulkan REQUIRED)

# Use FFmpeg 8.0.1 with Vulkan encoder support
set(FFMPEG_INCLUDE_DIRS "C:/ffmpeg-8.0.1-full_build-shared/include")
set(FFMPEG_LIBRARY_DIRS "C:/ffmpeg-8.0.1-full_build-shared/lib")
set(FFMPEG_LIBRARIES avformat avcodec avutil swscale swresample)

message(STATUS "Using FFmpeg 8.0.1 from: ${FFMPEG_INCLUDE_DIRS}")

# Enable CUDA if available (Phase 3 optimization)
# Use find_package instead of check_language for better VS generator# CUDA support with Toolkit detection
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    enable_language(CUDA)
    set(CMAKE_CUDA_STANDARD 11)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    # Let CUDA auto-detect GPU architecture
    set(CMAKE_CUDA_ARCHITECTURES OFF)
    
    set(CUDA_SOURCES
        CapCutClone/Encoder/CUDAFilters.cu
    )
    
    add_definitions(-DUSE_CUDA)
    message(STATUS "CUDA Toolkit found - GPU RGB→NV12 conversion enabled")
    message(STATUS "CUDA version: ${CUDAToolkit_VERSION}")
    message(STATUS "CUDA path: ${CUDAToolkit_BIN_DIR}")
else()
    set(CUDA_SOURCES "")
    message(WARNING "CUDA Toolkit not found - using CPU fallback for color conversion")
    message(STATUS "To enable CUDA: Install CUDA Toolkit 11.0+ and reconfigure")
endif()

# Vulkan support for Phase 1 (200+ FPS target pipeline)
find_package(Vulkan QUIET)
if(Vulkan_FOUND)
    add_definitions(-DUSE_VULKAN)
    message(STATUS "Vulkan SDK found - GPU-only pipeline available")
    message(STATUS "Vulkan version: ${Vulkan_VERSION}")
    message(STATUS "Vulkan include: ${Vulkan_INCLUDE_DIRS}")
    
    # Find glslc shader compiler
    # Add explicit path from user installation
    find_program(GLSLC_EXECUTABLE NAMES glslc HINTS "C:/VulkanSDK/1.4.335.0/Bin" "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(GLSLC_EXECUTABLE)
        message(STATUS "Found glslc: ${GLSLC_EXECUTABLE}")
        
        # Shader compilation command
        set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/CapCutClone/Vulkan/Shaders/RGB_to_NV12.comp")
        set(SHADER_BINARY "${CMAKE_CURRENT_BINARY_DIR}/CapCutClone/Vulkan/Shaders/RGB_to_NV12.spv")
        
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/CapCutClone/Vulkan/Shaders")
        
        add_custom_command(
            OUTPUT ${SHADER_BINARY}
            COMMAND ${GLSLC_EXECUTABLE} ${SHADER_SOURCE} -o ${SHADER_BINARY} --target-env=vulkan1.1
            DEPENDS ${SHADER_SOURCE}
            COMMENT "Compiling compute shader to SPIR-V binary"
        )
        
        # Create a custom target to enforce generation
        add_custom_target(CompileShaders DEPENDS ${SHADER_BINARY})
    else()
        message(WARNING "glslc not found! Compute shaders will not be compiled. Check VULKAN_SDK env var.")
    endif()
else()
    message(STATUS "Vulkan SDK not found - will use OpenGL+CUDA pipeline")
endif()



# Add all source files
set(VULKAN_SOURCES "")
if(Vulkan_FOUND)
    set(VULKAN_SOURCES
        CapCutClone/Vulkan/VulkanContext.cpp
        CapCutClone/Vulkan/VulkanRenderPipeline.cpp
        CapCutClone/Vulkan/VulkanComputePipeline.cpp
        CapCutClone/Vulkan/VulkanExportManager.cpp
        CapCutClone/Vulkan/VulkanTest.cpp
    )
    
    # Ensure shader is compiled before building sources
    if(GLSLC_EXECUTABLE)
        # set_source_files_properties(CapCutClone/Vulkan/VulkanComputePipeline.cpp PROPERTIES GENERATED TRUE)
        # Dependency added via add_dependencies below
    endif()
endif()

add_executable(CapCutClone 
    CapCutClone/CapCutClone.cpp
    CapCutClone/Application.cpp
    CapCutClone/UI/UIManager.cpp
    CapCutClone/UI/TimelineThumbnails.cpp
    CapCutClone/Video/VideoPlayer.cpp
    CapCutClone/Rendering/TextureRenderer.cpp
    CapCutClone/Timeline/TimelineManager.cpp
    CapCutClone/Timeline/EffectLayer.cpp
    CapCutClone/Encoder/HardwareExportManager.cpp
    CapCutClone/Configuration.cpp
    CapCutClone/Audio/AudioContext.cpp
    ${CUDA_SOURCES}
    ${VULKAN_SOURCES}
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

# Link Vulkan (REQUIRED - always available)
target_link_libraries(CapCutClone PRIVATE Vulkan::Vulkan)
target_compile_definitions(CapCutClone PRIVATE 
    HAVE_VULKAN=1
    VK_USE_PLATFORM_WIN32_KHR
)
message(STATUS "Vulkan hardware acceleration enabled")

# Link FFmpeg 8.0.1 libraries
target_link_directories(CapCutClone PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(CapCutClone PRIVATE ${FFMPEG_LIBRARIES})

if(TARGET CompileShaders)
    add_dependencies(CapCutClone CompileShaders)
endif()

# Link CUDA runtime if available
if(CUDAToolkit_FOUND)
    # Use STATIC runtime to avoid DLL dependency
    target_link_libraries(CapCutClone PRIVATE CUDA::cudart_static)
    message(STATUS "Linked CUDA runtime library (static)")
endif()

# Link Vulkan if available
if(Vulkan_FOUND)
    target_link_libraries(CapCutClone PRIVATE Vulkan::Vulkan)
    target_include_directories(CapCutClone PRIVATE ${Vulkan_INCLUDE_DIRS})
    message(STATUS "Linked Vulkan library")
endif()


# Post-build command: Copy FFmpeg 8.0.1 DLLs to output directory
add_custom_command(TARGET CapCutClone POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "C:/ffmpeg-8.0.1-full_build-shared/bin/avcodec-62.dll"
        "C:/ffmpeg-8.0.1-full_build-shared/bin/avformat-62.dll"
        "C:/ffmpeg-8.0.1-full_build-shared/bin/avutil-60.dll"
        "C:/ffmpeg-8.0.1-full_build-shared/bin/swresample-6.dll"
        "C:/ffmpeg-8.0.1-full_build-shared/bin/swscale-9.dll"
        "C:/ffmpeg-8.0.1-full_build-shared/bin/avdevice-62.dll"
        "C:/ffmpeg-8.0.1-full_build-shared/bin/avfilter-11.dll"
        $<TARGET_FILE_DIR:CapCutClone>
    COMMENT "Copying FFmpeg 8.0.1 DLLs with Vulkan encoder support"
)

# Copy compiled shader if present
if(GLSLC_EXECUTABLE)
    add_custom_command(TARGET CapCutClone POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/CapCutClone/Vulkan/Shaders/RGB_to_NV12.spv"
        $<TARGET_FILE_DIR:CapCutClone>/RGB_to_NV12.spv
        COMMENT "Copying computed shader binary"
    )
endif()

# Copy CUDA DLLs if CUDA is enabled
# Commented out - CUDA DLL may already be in system PATH
# if(CUDAToolkit_FOUND)
#     add_custom_command(TARGET CapCutClone POST_BUILD
#         COMMAND ${CMAKE_COMMAND} -E copy_if_different
#             "${CUDAToolkit_BIN_DIR}/cudart64_13.dll"
#             $<TARGET_FILE_DIR:CapCutClone>
#         COMMENT "Copying CUDA runtime DLL"
#     )
# endif()
