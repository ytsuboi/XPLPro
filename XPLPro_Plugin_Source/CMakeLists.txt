cmake_minimum_required(VERSION 3.15)

# macOS: universal binary and deployment target must be set before project()
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "Build universal binary for X-Plane 12")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(XPLProPlugin LANGUAGES CXX)

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

    target_link_libraries(XPLProPlugin PRIVATE
        XPLM_64
        XPWidgets_64
        ${CMAKE_CURRENT_SOURCE_DIR}/libconfig.lib
        OpenGL32
    )

    set_target_properties(XPLProPlugin PROPERTIES
        OUTPUT_NAME "win"
        SUFFIX ".xpl"
        PREFIX ""
    )

elseif(APPLE)
    # macOS (APL)
    # libconfig is required. Install via: brew install libconfig
    # Static link so the plugin is self-contained (no dylib dependency for users)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBCONFIG REQUIRED libconfig)

    # Find the static library (.a) for libconfig
    find_library(LIBCONFIG_STATIC_LIB libconfig.a
        HINTS ${LIBCONFIG_LIBRARY_DIRS} /usr/local/lib
    )

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
        LINK_FLAGS "-flat_namespace -undefined suppress"
    )

endif()

# ---- No lib prefix on any platform ----
set_target_properties(XPLProPlugin PROPERTIES
    PREFIX ""
)
