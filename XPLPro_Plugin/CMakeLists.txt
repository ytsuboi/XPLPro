cmake_minimum_required(VERSION 3.15)

# macOS: universal binary and deployment target must be set before project()
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build universal binary for X-Plane 12")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(XPLProPlugin LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Source files ----
set(SOURCES
    XPLProPlugin.cpp
    XPLDevice.cpp
    DataTransfer.cpp
    SerialClass.cpp
    Config.cpp
    StatusWindow.cpp
    abbreviations.cpp
)

# ---- Build as shared library (.xpl) ----
add_library(XPLProPlugin SHARED ${SOURCES})

# ---- X-Plane SDK include paths ----
target_include_directories(XPLProPlugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/SDK/CHeaders/XPLM
    ${CMAKE_CURRENT_SOURCE_DIR}/SDK/CHeaders/Widgets
    ${CMAKE_CURRENT_SOURCE_DIR}/SDK/CHeaders/Wrappers
    ${CMAKE_CURRENT_SOURCE_DIR}/libconfig-1.7.3/lib
)

# ---- Common definitions ----
target_compile_definitions(XPLProPlugin PRIVATE
    XPLM200=1
    XPLM100=1
)

# ---- Platform-specific settings ----
if(WIN32)
    # Windows (IBM)
    target_compile_definitions(XPLProPlugin PRIVATE
        IBM=1
        APL=0
        WIN32
        _CRT_SECURE_NO_WARNINGS
        WINVER=0x0601
        _WIN32_WINNT=0x0601
    )

    target_link_directories(XPLProPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/SDK/Libraries/Win
    )

    # ---- Build libconfig as a static library from source ----
    set(LIBCONFIG_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libconfig-1.7.3/lib)
    add_library(libconfig_static STATIC
        ${LIBCONFIG_SRC_DIR}/grammar.c
        ${LIBCONFIG_SRC_DIR}/libconfig.c
        ${LIBCONFIG_SRC_DIR}/scanctx.c
        ${LIBCONFIG_SRC_DIR}/scanner.c
        ${LIBCONFIG_SRC_DIR}/strbuf.c
        ${LIBCONFIG_SRC_DIR}/strvec.c
        ${LIBCONFIG_SRC_DIR}/util.c
        ${LIBCONFIG_SRC_DIR}/wincompat.c
    )
    set_target_properties(libconfig_static PROPERTIES LANGUAGE C)
    target_include_directories(libconfig_static PRIVATE ${LIBCONFIG_SRC_DIR})
    target_compile_definitions(libconfig_static PRIVATE
        LIBCONFIG_STATIC
        _CRT_SECURE_NO_WARNINGS
        WIN32
        YY_NO_UNISTD_H
    )

    target_compile_definitions(XPLProPlugin PRIVATE LIBCONFIG_STATIC)

    target_link_libraries(XPLProPlugin PRIVATE
        XPLM_64
        XPWidgets_64
        libconfig_static
        OpenGL32
        Shlwapi
    )

    set_target_properties(XPLProPlugin PROPERTIES
        OUTPUT_NAME "win"
        SUFFIX ".xpl"
        PREFIX ""
    )

elseif(APPLE)
    # macOS (APL)
    # libconfig headers are obtained from Homebrew (brew install libconfig).
    # For linking, we use a vendored universal (x86_64+arm64) libconfig.a at
    # the project root, so Intel Homebrew's x86_64-only .a isn't a problem and
    # distributed plugins are fully self-contained.
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCONFIG REQUIRED libconfig)

    set(LIBCONFIG_STATIC_LIB "${CMAKE_CURRENT_SOURCE_DIR}/libconfig_mac.a")
    if(NOT EXISTS "${LIBCONFIG_STATIC_LIB}")
        message(FATAL_ERROR "Vendored universal libconfig static library not found: ${LIBCONFIG_STATIC_LIB}")
    endif()

    target_compile_definitions(XPLProPlugin PRIVATE
        APL=1
        IBM=0
    )

    target_include_directories(XPLProPlugin PRIVATE
        ${LIBCONFIG_INCLUDE_DIRS}
    )

    # X-Plane SDK frameworks
    set(XPLM_FRAMEWORK "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Libraries/Mac/XPLM.framework")
    set(XPWIDGETS_FRAMEWORK "${CMAKE_CURRENT_SOURCE_DIR}/SDK/Libraries/Mac/XPWidgets.framework")

    target_link_libraries(XPLProPlugin PRIVATE
        ${XPLM_FRAMEWORK}
        ${XPWIDGETS_FRAMEWORK}
        ${LIBCONFIG_STATIC_LIB}
        "-framework OpenGL"
        "-framework IOKit"
        "-framework CoreFoundation"
    )

    # X-Plane plugins use flat_namespace (symbols resolved at load time by X-Plane)
    set_target_properties(XPLProPlugin PROPERTIES
        OUTPUT_NAME "mac"
        SUFFIX ".xpl"
        PREFIX ""
        LINK_FLAGS "-undefined dynamic_lookup"
    )

endif()

# ---- No lib prefix on any platform ----
set_target_properties(XPLProPlugin PROPERTIES
    PREFIX ""
)
