cmake_minimum_required(VERSION 3.19)
project(tvpgl_simd LANGUAGES CXX)

set(SIMD_PATH ${CMAKE_CURRENT_LIST_DIR})
set(SIMD_SOURCE_FILES
    ${SIMD_PATH}/tvpgl_simd_init.cpp
    ${SIMD_PATH}/tvpgl_simd_copy.cpp
    ${SIMD_PATH}/tvpgl_simd_alpha_blend.cpp
    ${SIMD_PATH}/tvpgl_simd_arithmetic_blend.cpp
)

add_library(${PROJECT_NAME} STATIC ${SIMD_SOURCE_FILES})

# Highway dependency via vcpkg
find_package(hwy CONFIG REQUIRED)
target_link_libraries(${PROJECT_NAME} PRIVATE hwy::hwy)

# Include paths: public for tvpgl_simd_init.h, private for engine headers
target_include_directories(${PROJECT_NAME} PUBLIC ${SIMD_PATH})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/..      # visual/ (for tvpgl.h)
    ${CMAKE_CURRENT_LIST_DIR}/../gl   # visual/gl/ (for blend headers)
)

# Need tjs2 for tjsTypes.h
target_link_libraries(${PROJECT_NAME} PRIVATE tjs2)

# C++17 required by Highway
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_17)

# Static dispatch: compile only for the current target architecture
# ARM64 always has NEON; x86_64 targets SSE4.1+ or AVX2
target_compile_definitions(${PROJECT_NAME} PRIVATE
    HWY_COMPILE_ONLY_STATIC=1
)
