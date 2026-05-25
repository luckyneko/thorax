cmake_minimum_required(VERSION 3.20...3.28)

# disable testsuite if bundled
if(NOT DEFINED PROJECT_NAME)
  set(NOT_SUBPROJECT ON)
endif()

# Disable building in SRC
if (CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

# Default build release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

project(thorax LANGUAGES CXX VERSION 0.0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")
include(thx_warnings)
include(thx_plugin_manifest)
include(thx_plugin_auto_manifest)

# GCC 9.1 is the minimum version that supports __PRETTY_FUNCTION__ in constexpr.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.1")
    message(FATAL_ERROR "GCC >= 9.1 is required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()

# Options
option(THORAX_BUILD_TESTING "Build UnitTesting project" ${NOT_SUBPROJECT})
option(THORAX_BUILD_EXAMPLES "Build example applications" ${NOT_SUBPROJECT})
option(THORAX_BUILD_PLUGINS "Build built-in plugins" ${NOT_SUBPROJECT})
option(THORAX_INSTALL "Generate install targets and CMake package files" ${NOT_SUBPROJECT})
option(THORAX_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer (Clang/GCC only)" OFF)

include(GNUInstallDirs)

# Load Catch2
if(THORAX_BUILD_TESTING)
    include(addcatch2)
endif()

# Generate version header from project VERSION field
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/thx/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/thx/version.h"
    @ONLY
)

# Static Library Target
find_package(Threads REQUIRED)
add_library(${PROJECT_NAME} STATIC
    # Headers — top-level (cross-cutting types and umbrella)
    include/thx/thorax.h
    include/thx/string_view.h
    include/thx/span.h
    include/thx/version_type.h
    include/thx/version.h.in
    include/thx/to_string.h
    include/thx/result.h
    include/thx/library.h
    include/thx/registry.h
    include/thx/log.h

    # Headers — service/
    include/thx/service/iservice.h
    include/thx/service/service.h
    include/thx/service/service_id.h
    include/thx/service/service_manager.h
    include/thx/service/service_manager.inl

    # Headers — plugin/
    include/thx/plugin/iplugin.h
    include/thx/plugin/manifest.h
    include/thx/plugin/platform.h
    include/thx/plugin/plugin_handle.h
    include/thx/plugin/plugin_garbage.h
    include/thx/plugin/plugin_manager.h
    include/thx/plugin/plugin.h

    # Headers — rtti/
    include/thx/rtti/type_name.h

    # Source
    src/thorax.cpp
    src/log.cpp
    src/library.cpp
    src/registry.cpp
    src/service/service_manager.cpp
    src/plugin/plugin_handle.cpp
    src/plugin/plugin_garbage.cpp
    src/plugin/plugin_manager.cpp
    src/plugin/manifest.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(${PROJECT_NAME} PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Force C++17
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# Enable FPIC
set_target_properties(${PROJECT_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Set default warnings
thx_set_warnings(${PROJECT_NAME})

# Handle Sanitization
if(THORAX_SANITIZE)
    if(MSVC)
        message(WARNING "THORAX_SANITIZE is not supported with MSVC; ignored.")
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Framework tools (under tools/<name>/). thx_emit_manifest is required by
# thx_plugin_auto_manifest, so the tools tree is always added unconditionally
# — even out-of-tree consumers that import Thorax expect the tools targets
# to be present.
add_subdirectory(tools)

# Examples
if(THORAX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Plugins
if(THORAX_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

# Tests (mock plugins, test exe, install smoke test)
if(THORAX_BUILD_TESTING)
    enable_testing()
    add_subdirectory(test)
endif()

# Install + CPack package rules
if(THORAX_INSTALL)
    include(thx_install)
endif()
