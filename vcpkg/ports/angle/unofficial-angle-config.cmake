include(CMakeFindDependencyMacro)
find_dependency(ZLIB)

# For static builds, transitive dependencies must be found by the consumer.
# Vulkan dependencies are needed when ANGLE was built with Vulkan backend (e.g. Android).
if(ANDROID)
    find_dependency(VulkanHeaders CONFIG)
    find_dependency(VulkanMemoryAllocator CONFIG)
    find_dependency(SPIRV-Tools CONFIG)
endif()

include("${CMAKE_CURRENT_LIST_DIR}/unofficial-angle-targets.cmake")
