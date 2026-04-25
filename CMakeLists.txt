cmake_minimum_required(VERSION 3.15...3.28)

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

# GCC 9.1 is the minimum version that supports __PRETTY_FUNCTION__ in constexpr.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.1")
    message(FATAL_ERROR "GCC >= 9.1 is required (found ${CMAKE_CXX_COMPILER_VERSION})")
endif()

# Options
option(THORAX_BUILD_TESTING "Build UnitTesting project" ${NOT_SUBPROJECT})
option(THORAX_BUILD_EXAMPLES "Build example applications" ${NOT_SUBPROJECT})
option(THORAX_BUILD_PLUGINS "Build built-in plugins" ${NOT_SUBPROJECT})
option(THORAX_SANITIZE "Enable AddressSanitizer and UndefinedBehaviorSanitizer (Clang/GCC only)" OFF)

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
    # Headers
    include/thx/detail/hash.h
    include/thx/detail/semver.h
    include/thx/detail/type_name.h
    include/thx/thorax.h
    include/thx/string_view.h
    include/thx/span.h
    include/thx/service_id.h
    include/thx/version_type.h
    include/thx/version.h.in
    include/thx/iservice.h
    include/thx/service.h
    include/thx/service_manager.h
    include/thx/platform.h
    include/thx/result.h
    include/thx/plugin_handle.h
    include/thx/plugin_loader.h
    include/thx/log.h

    # Source
    src/thorax.cpp
    src/log.cpp
    src/service_manager.cpp
    src/plugin_handle.cpp
    src/plugin_loader.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC
    include
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)
target_link_libraries(${PROJECT_NAME} PUBLIC
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

# GCC < 9 requires explicit linking for std::filesystem.
if(CMAKE_COMPILER_IS_GNUCXX AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
    target_link_libraries(${PROJECT_NAME} PUBLIC stdc++fs)
endif()

# Force C++17
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# Enable Warnings as errors
if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(${PROJECT_NAME} PRIVATE /WX /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Werror -Wall -Wextra)
endif()

# Handle Sanitization
if(THORAX_SANITIZE)
    if(MSVC)
        message(WARNING "THORAX_SANITIZE is not supported with MSVC; ignored.")
    else()
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

# Examples
if(THORAX_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Plugins
if(THORAX_BUILD_PLUGINS)
    add_subdirectory(plugins)
endif()

# Testing Target
if(THORAX_BUILD_TESTING)
    enable_testing()

    # Mock plugin shared library for PluginLoader integration tests.
    add_library(mock_plugin SHARED
        test/mock_plugin/mock_plugin.cpp
    )
    target_link_libraries(mock_plugin PRIVATE ${PROJECT_NAME})
    target_include_directories(mock_plugin PRIVATE test/mock_plugin)

    # Mock plugin that exports thx_abi_version() = 999 to test version-mismatch rejection.
    add_library(mock_plugin_bad_abi SHARED
        test/mock_plugin_bad_abi/mock_bad_abi.cpp
    )
    target_link_libraries(mock_plugin_bad_abi PRIVATE ${PROJECT_NAME})

    add_executable(test-${PROJECT_NAME}
        test/thorax.cpp
        test/test_service_id.cpp
        test/test_version.cpp
        test/test_service_manager.cpp
        test/test_abi_types.cpp
        test/test_plugin_loader.cpp
        test/test_diagnostics.cpp
    )
    target_link_libraries(test-${PROJECT_NAME} ${PROJECT_NAME} Catch2::Catch2WithMain)
    target_include_directories(test-${PROJECT_NAME} PRIVATE test/mock_plugin)
    # Provide the built mock plugin paths at compile time.
    target_compile_definitions(test-${PROJECT_NAME} PRIVATE
        THX_MOCK_PLUGIN_PATH="$<TARGET_FILE:mock_plugin>"
        THX_MOCK_BAD_ABI_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_bad_abi>"
    )
    add_dependencies(test-${PROJECT_NAME} mock_plugin mock_plugin_bad_abi)

    if(THORAX_BUILD_PLUGINS)
        target_sources(test-${PROJECT_NAME} PRIVATE
            test/test_logging_plugin.cpp
            test/test_io_plugin.cpp
        )
        target_include_directories(test-${PROJECT_NAME} PRIVATE
            plugins/logging/include
            plugins/io/include
        )
        target_compile_definitions(test-${PROJECT_NAME} PRIVATE
            THX_LOGGING_PLUGIN_PATH="$<TARGET_FILE:plugin_logging>"
            THX_IO_PLUGIN_PATH="$<TARGET_FILE:plugin_io>"
        )
        add_dependencies(test-${PROJECT_NAME} plugin_logging plugin_io)
    endif()

    catch_discover_tests(test-${PROJECT_NAME} TEST_PREFIX "(${PROJECT_NAME}) ")
endif()
