cmake_minimum_required(VERSION 3.28)

find_program(CCACHE_PROGRAM ccache)

if (CCACHE_PROGRAM)
    set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    message(STATUS "Ccache found: ${CCACHE_PROGRAM}")
endif ()

add_compile_definitions(
        $<$<CONFIG:Debug>:DEBUG>
        $<$<CONFIG:Debug>:_DEBUG>   # 对于 MSVC 兼容性
        $<$<NOT:$<CONFIG:Debug>>:NDEBUG>
)

if(ANDROID)
    set(VCPKG_TARGET_ANDROID ON)
    set(ENV{ANDROID_NDK_HOME} "$ENV{ANDROID_NDK}")
    add_compile_options(-Wno-inconsistent-missing-override)
    include(cmake/vcpkg_android.cmake)
else()
    set(CMAKE_TOOLCHAIN_FILE $ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
endif()

set(APP_NAME krkr2)

project(${APP_NAME})

option(ENABLE_TESTS "enable tests execute build(exclude android)" ON)
option(BUILD_TOOLS "build tools execute build(exclude android ios)" ON)
option(BUILD_ENGINE_API "build engine_api bridge shared library" ON)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(KRKR2CORE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/core)
set(KRKR2PLUGIN_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cpp/plugins)

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/EHsc>" "/MP" "/utf-8")
    add_link_options("/ignore:4099" "/INCREMENTAL" "/DEBUG:FASTLINK")
    # set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

if(ANDROID)
    add_library(${PROJECT_NAME} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/platforms/android/cpp/krkr2_android.cpp)
    find_package(unofficial-breakpad CONFIG REQUIRED)
    target_link_libraries(${PROJECT_NAME} PUBLIC unofficial::breakpad::libbreakpad_client)
elseif(LINUX)
    add_executable(${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/platforms/linux/main.cpp)
elseif(WINDOWS)
    add_executable(${PROJECT_NAME}
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/windows/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/platforms/windows/game.rc
    )
elseif(IOS)
    # iOS uses Flutter as the app shell; only build engine libraries.
    # engine_api is built as a static library and linked into the Flutter Runner.
elseif(MACOS)
    # macOS uses Flutter as the app shell; only build engine libraries.
    # The standalone krkr2 executable (cocos2dx) is no longer produced.
endif()

# external lib
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cpp/external)

# build library
add_subdirectory(${KRKR2CORE_PATH})
add_subdirectory(${KRKR2PLUGIN_PATH})
if(BUILD_ENGINE_API)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/bridge/engine_api)
endif()

# Link the standalone executable target (not needed on macOS where Flutter is the app shell)
if(TARGET ${PROJECT_NAME})
    target_link_libraries(${PROJECT_NAME} PUBLIC
        krkr2plugin krkr2core
    )

    ## app config
    if(APPLE)
        set_target_properties(${APP_NAME} PROPERTIES RESOURCE "${APP_UI_RES}")
        if(MACOSX)
            set_target_properties(${APP_NAME} PROPERTIES
                    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/platforms/apple/macos/Info.plist"
            )
        endif()
    endif()
endif()

if(ENABLE_TESTS AND NOT (IOS OR ANDROID))
    enable_testing()
    add_subdirectory(tests)
endif()

if(BUILD_TOOLS AND NOT (IOS OR ANDROID))
    add_subdirectory(tools)
endif()
