
# Android platform configuration for ANGLE
# Uses ANGLE's own EGL implementation (NOT system EGL) to avoid conflicts
# with Flutter's Impeller renderer which also uses the system EGL.
# Backends:
#   - OpenGL ES (via ANGLE GL backend loading native GLES driver)
#   - Vulkan    (via ANGLE Vulkan backend, optional, controlled by USE_VULKAN)

list(APPEND ANGLE_DEFINITIONS ANGLE_PLATFORM_ANDROID)

include(linux.cmake)

if (USE_ANGLE_EGL OR ENABLE_WEBGL)
    list(APPEND ANGLE_SOURCES
        ${gl_backend_sources}

        ${angle_system_utils_sources_linux}
        ${angle_system_utils_sources_posix}

        ${angle_dma_buf_sources}

        ${libangle_gl_egl_dl_sources}
        ${libangle_gl_egl_sources}
        ${libangle_gl_sources}

        ${libangle_gpu_info_util_sources}
        ${libangle_gpu_info_util_linux_sources}
    )

    list(APPEND ANGLE_DEFINITIONS
        ANGLE_ENABLE_OPENGL
    )

    # Enable GLSL compiler output
    list(APPEND ANGLE_DEFINITIONS ANGLE_ENABLE_GLSL)
endif ()

# Vulkan backend (optional, enabled via USE_VULKAN)
if (USE_VULKAN)
    find_package(VulkanHeaders CONFIG REQUIRED)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    find_package(SPIRV-Tools CONFIG REQUIRED)

    # Core Vulkan backend sources (generated from vulkan_backend.gni by gni-to-cmake.py)
    # Note: Android-specific sources (AHBFunctions, DisplayVkAndroid, etc.) are already
    # conditionally included in Vulkan.cmake via the if(is_android) block.
    list(APPEND ANGLE_SOURCES
        ${vulkan_backend_sources}
    )

    # volk: Vulkan meta-loader. On Android, ANGLE uses ANGLE_SHARED_LIBVULKAN mode
    # which loads libvulkan.so via dlopen and resolves all Vulkan functions dynamically
    # through vkGetInstanceProcAddr. This is required because Android API level 24's
    # libvulkan.so only exports Vulkan 1.0 core functions.
    list(APPEND ANGLE_SOURCES
        src/third_party/volk/volk.c
        src/third_party/volk/volk.h
    )

    # GPU info utilities (provides ParseArmVulkanDriverVersion, ParseQualcommVulkanDriverVersion, etc.)
    list(APPEND ANGLE_SOURCES
        src/gpu_info_util/SystemInfo_vulkan.cpp
        src/gpu_info_util/SystemInfo_vulkan.h
    )

    # VMA (Vulkan Memory Allocator) wrapper
    list(APPEND ANGLE_SOURCES
        src/libANGLE/renderer/vulkan/vk_mem_alloc_wrapper.cpp
        src/libANGLE/renderer/vulkan/vk_mem_alloc_wrapper.h
    )

    # Common Vulkan utility sources
    list(APPEND ANGLE_SOURCES
        src/common/vulkan/libvulkan_loader.cpp
        src/common/vulkan/libvulkan_loader.h
        src/common/vulkan/vulkan_icd.cpp
        src/common/vulkan/vulkan_icd.h
    )

    # SPIRV utility sources
    list(APPEND ANGLE_SOURCES
        src/common/spirv/angle_spirv_utils.cpp
        src/common/spirv/spirv_types.h
        src/common/spirv/spirv_instruction_builder_autogen.cpp
        src/common/spirv/spirv_instruction_builder_autogen.h
        src/common/spirv/spirv_instruction_parser_autogen.cpp
        src/common/spirv/spirv_instruction_parser_autogen.h
    )

    # SPIRV translator sources (required for GLSL->SPIRV compilation when Vulkan is enabled)
    # Corresponds to angle_translator_lib_spirv_sources in compiler.gni
    list(APPEND ANGLE_SOURCES
        ${angle_translator_lib_spirv_sources}
    )

    list(APPEND ANGLE_DEFINITIONS
        ANGLE_ENABLE_VULKAN
        ANGLE_USE_CUSTOM_VULKAN_OUTSIDE_RENDER_PASS_CMD_BUFFERS=1
        ANGLE_USE_CUSTOM_VULKAN_RENDER_PASS_CMD_BUFFERS=1
        ANGLE_ENABLE_CRC_FOR_PIPELINE_CACHE
        VK_USE_PLATFORM_ANDROID_KHR
        # Use volk to dynamically load Vulkan functions (matches GN: angle_shared_libvulkan = !is_mac).
        # This avoids linking against libvulkan.so which on API 24 only has Vulkan 1.0 symbols.
        ANGLE_SHARED_LIBVULKAN=1
        VK_NO_PROTOTYPES  # Required by volk: disables Vulkan function prototypes from vulkan.h
        VMA_IMPLEMENTATION  # Required: VMA is header-only; this enables function definitions in vk_mem_alloc.h
    )

    list(APPEND ANGLE_PRIVATE_INCLUDE_DIRECTORIES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/common/vulkan"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/libANGLE/renderer/vulkan"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/third_party/volk"  # volk.h include path
    )

    # Link Vulkan-related libraries
    # Note: NOT linking vulkan (libvulkan.so) directly because ANGLE_SHARED_LIBVULKAN
    # uses volk to dlopen/dlsym all Vulkan functions at runtime.
    list(APPEND ANGLEGLESv2_LIBRARIES
        Vulkan::Headers
        GPUOpen::VulkanMemoryAllocator
        SPIRV-Tools-static
    )

    # Enable GLSL->SPIRV translator for Vulkan
    list(APPEND ANGLE_DEFINITIONS ANGLE_ENABLE_GLSL)
endif ()

# Android system libraries
list(APPEND ANGLEGLESv2_LIBRARIES log android)
