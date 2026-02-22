find_package(ZLIB REQUIRED)

# Use platform-specific gpu_info_util and sources
if(IOS)
    list(APPEND ANGLE_SOURCES
        ${libangle_gpu_info_util_ios_sources}
        ${libangle_gpu_info_util_sources}
    )
else()
    list(APPEND ANGLE_SOURCES
        ${libangle_gpu_info_util_mac_sources}
        ${libangle_gpu_info_util_sources}
        ${libangle_mac_sources}
    )
endif()

list(APPEND ANGLEGLESv2_LIBRARIES
    "-framework CoreGraphics"
    "-framework Foundation"
    "-framework IOSurface"
)

# IOKit and Quartz frameworks are only available on macOS, not iOS
if(NOT IOS)
    list(APPEND ANGLEGLESv2_LIBRARIES
        "-framework IOKit"
        "-framework Quartz"
    )
else()
    list(APPEND ANGLEGLESv2_LIBRARIES
        "-framework UIKit"
    )
endif()

# Metal backend
if(USE_METAL)
    list(APPEND ANGLE_SOURCES
        ${metal_backend_sources}

        ${angle_translator_lib_msl_sources}

        ${angle_translator_glsl_apple_sources}
    )

    list(APPEND ANGLE_DEFINITIONS
        ANGLE_ENABLE_METAL
    )

    list(APPEND ANGLEGLESv2_LIBRARIES
        "-framework Metal"
    )
endif()

# OpenGL backend (macOS only, not available on iOS)
if(USE_OPENGL)
    list(APPEND ANGLE_SOURCES
        ${angle_translator_glsl_base_sources}
        ${angle_translator_glsl_sources}
    )
    # Enable GLSL compiler output.
    if(IOS)
        list(APPEND ANGLE_DEFINITIONS ANGLE_ENABLE_GLSL ANGLE_ENABLE_APPLE_WORKAROUNDS ANGLE_ENABLE_EAGL)
    else()
        list(APPEND ANGLE_DEFINITIONS ANGLE_ENABLE_GLSL ANGLE_ENABLE_GL_DESKTOP_BACKEND ANGLE_ENABLE_APPLE_WORKAROUNDS ANGLE_ENABLE_CGL)
    endif()
endif()

if(USE_OPENGL OR ENABLE_WEBGL)
    list(APPEND ANGLE_SOURCES
        ${gl_backend_sources}

        ${libangle_gl_egl_dl_sources}
        ${libangle_gl_egl_sources}
        ${libangle_gl_sources}
    )

    list(APPEND ANGLE_DEFINITIONS
        ANGLE_ENABLE_OPENGL
    )
endif()
