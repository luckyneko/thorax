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

# Windows has no rpath: a DLL's dependencies are resolved from the directory of
# the loading executable (and PATH). Co-locate every runtime artifact — the
# library, the test binary, the thx_emit_manifest tool, and all plugin DSOs —
# in one directory so each finds thorax.dll without manual PATH munging. This
# is what lets the thx_emit_manifest POST_BUILD step (which dlopens a freshly
# built plugin that links thorax.dll) run during the build. Unix relies on
# rpath instead and keeps its per-target layout.
if(WIN32 AND NOT_SUBPROJECT)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()

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
set(THORAX_SANITIZE OFF CACHE STRING
    "Sanitizers (Clang/GCC only): OFF, ON (= address,undefined), or any -fsanitize list e.g. 'thread'")

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
add_library(${PROJECT_NAME} SHARED
    # Public headers — top-level (cross-cutting types and umbrella)
    include/thx/thorax.h
    include/thx/thx_api.h
    include/thx/lifecycle.h
    include/thx/string_view.h
    include/thx/span.h
    include/thx/version_type.h
    include/thx/version.h.in
    include/thx/to_string.h
    include/thx/result.h

    # Public headers — log/
    include/thx/log/log.h
    include/thx/log/log_level.h
    include/thx/log/log_record.h
    include/thx/log/log_service.h
    include/thx/log/source_location.h

    # Public headers — service/
    include/thx/service/iservice.h
    include/thx/service/service.h
    include/thx/service/service_id.h

    # Public headers — plugin/
    include/thx/plugin/iplugin.h
    include/thx/plugin/manifest.h
    include/thx/plugin/platform.h
    include/thx/plugin/plugin.h

    # Public headers — rtti/
    include/thx/rtti/type_name.h

    # Private headers
    src/library.h
    src/registry.h
    src/service/service_manager.h
    src/service/service_manager.inl
    src/plugin/plugin_handle.h
    src/plugin/plugin_garbage.h
    src/plugin/plugin_manager.h

    # Source
    src/abi.cpp
    src/thorax.cpp
    src/log/log.cpp
    src/library.cpp
    src/registry.cpp
    src/service/service_manager.cpp
    src/service/service.cpp
    src/plugin/plugin_handle.cpp
    src/plugin/plugin_garbage.cpp
    src/plugin/plugin_manager.cpp
    src/plugin/plugin.cpp
    src/plugin/manifest.cpp
)
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_link_libraries(${PROJECT_NAME} PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# Force C++17
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# Namespaced alias so add_subdirectory consumers can link Thorax::thorax — the
# same target name that find_package(Thorax) consumers get from the export set.
add_library(Thorax::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Visibility: hide everything by default; only THX_API-decorated symbols cross
# the boundary. Inert while the library is STATIC; live once it becomes SHARED.
# THX_BUILDING flips THX_API to dllexport on Windows.
# THX_TESTING enables THX_INTERNAL_API expansion — internal classes get
# default visibility so the in-tree test binary can link against them.
# Defined here on the library target AND on the test target (below) so the
# class definitions match between TUs. Off in production builds — internal
# classes stay hidden in the .so's export table.
target_compile_definitions(${PROJECT_NAME} PRIVATE
    THX_BUILDING
    $<$<BOOL:${THORAX_BUILD_TESTING}>:THX_TESTING>
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
    POSITION_INDEPENDENT_CODE ON
    VERSION                   ${PROJECT_VERSION}
    SOVERSION                 ${PROJECT_VERSION_MAJOR}
    # macOS: install_name uses @rpath so consumers locate the lib via RPATH.
    MACOSX_RPATH              ON
)

# Set default warnings
thx_set_warnings(${PROJECT_NAME})

# Handle sanitization. THORAX_SANITIZE selects the -fsanitize set:
#   OFF (default) — none
#   ON            — address,undefined (back-compat with the original boolean)
#   <list>        — passed through verbatim, e.g. "thread"
if(THORAX_SANITIZE)
    if(MSVC)
        message(WARNING "THORAX_SANITIZE is not supported with MSVC; ignored.")
    else()
        if(THORAX_SANITIZE STREQUAL "ON" OR THORAX_SANITIZE STREQUAL "TRUE")
            set(_thx_sanitizers "address,undefined")
        else()
            set(_thx_sanitizers "${THORAX_SANITIZE}")
        endif()
        add_compile_options(-fsanitize=${_thx_sanitizers} -fno-omit-frame-pointer)
        add_link_options(-fsanitize=${_thx_sanitizers})
    endif()
endif()

# Framework tools (under tools/<name>/). thx_emit_manifest is required by
# thx_plugin_auto_manifest, so the tools tree is always added unconditionally
# — even out-of-tree consumers that import Thorax expect the tools targets
# to be present.
add_subdirectory(tools)

# Plugins — added before examples so the example host_logging target can
# reference the in-tree plugin_log_spdlog target (it loads it at runtime to
# demonstrate selecting a logging backend).
if(THORAX_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

# Examples
if(THORAX_BUILD_EXAMPLES)
    add_subdirectory(examples)
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
