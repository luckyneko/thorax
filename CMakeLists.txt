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
include(thx_warnings)
include(thx_plugin_manifest)

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

    # Mock plugin shared library for PluginManager integration tests.
    add_library(mock_plugin SHARED
        test/mock_plugin/mock_plugin.cpp
    )
    target_link_libraries(mock_plugin PRIVATE ${PROJECT_NAME})
    target_include_directories(mock_plugin PRIVATE test/mock_plugin)
    thx_set_warnings(mock_plugin)
    target_compile_options(mock_plugin PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_manifest(mock_plugin
        NAME     "thx_mock.MockService"
        VERSION  "1.0.0"
        PROVIDES "thx_mock.MockService"
    )

    # Mock plugin that exports thx_abi_version() = 999 to test version-mismatch rejection.
    add_library(mock_plugin_bad_abi SHARED
        test/mock_plugin_bad_abi/mock_bad_abi.cpp
    )
    target_link_libraries(mock_plugin_bad_abi PRIVATE ${PROJECT_NAME})
    thx_set_warnings(mock_plugin_bad_abi)
    target_compile_options(mock_plugin_bad_abi PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_manifest(mock_plugin_bad_abi
        NAME    "test.BadAbi"
        VERSION "1.0.0"
    )

    # IPlugin-based mock that registers ServiceA + ServiceB and requires MockService.
    add_library(mock_plugin_multi SHARED
        test/mock_plugin_multi/mock_plugin_multi.cpp
    )
    target_link_libraries(mock_plugin_multi PRIVATE ${PROJECT_NAME})
    target_include_directories(mock_plugin_multi PRIVATE test/mock_plugin)
    thx_set_warnings(mock_plugin_multi)
    target_compile_options(mock_plugin_multi PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_manifest(mock_plugin_multi
        NAME     "thx.mock.MultiPlugin"
        VERSION  "1.0.0"
        PROVIDES "thx_mock.ServiceA"
                 "thx_mock.ServiceB"
        REQUIRES "thx_mock.MockService:1.0.0"
    )

    # IPlugin-based mock whose onLoad returns false.
    add_library(mock_plugin_bails SHARED
        test/mock_plugin_bails/mock_plugin_bails.cpp
    )
    target_link_libraries(mock_plugin_bails PRIVATE ${PROJECT_NAME})
    thx_set_warnings(mock_plugin_bails)
    target_compile_options(mock_plugin_bails PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_manifest(mock_plugin_bails
        NAME    "thx.mock.BailingPlugin"
        VERSION "1.0.0"
    )

    # IPlugin-based mock that requires MockService at 2.0.0 (mock_plugin only
    # registers 1.0.0), used to test the versioned-requirement rejection path.
    add_library(mock_plugin_requires_newer SHARED
        test/mock_plugin_requires_newer/mock_plugin_requires_newer.cpp
    )
    target_link_libraries(mock_plugin_requires_newer PRIVATE ${PROJECT_NAME})
    target_include_directories(mock_plugin_requires_newer PRIVATE test/mock_plugin)
    thx_set_warnings(mock_plugin_requires_newer)
    target_compile_options(mock_plugin_requires_newer PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_manifest(mock_plugin_requires_newer
        NAME     "thx.mock.RequiresNewerPlugin"
        VERSION  "1.0.0"
        REQUIRES "thx_mock.MockService:2.0.0"
    )

    # IPlugin-based mock that registers ServiceA in onLoad and then returns false.
    # Used to verify PluginManager rolls back partial registrations on failure.
    add_library(mock_plugin_bails_after_register SHARED
        test/mock_plugin_bails_after_register/mock_plugin_bails_after_register.cpp
    )
    target_link_libraries(mock_plugin_bails_after_register PRIVATE ${PROJECT_NAME})
    target_include_directories(mock_plugin_bails_after_register PRIVATE test/mock_plugin)
    thx_set_warnings(mock_plugin_bails_after_register)
    target_compile_options(mock_plugin_bails_after_register PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_manifest(mock_plugin_bails_after_register
        NAME     "thx.mock.BailsAfterRegisterPlugin"
        VERSION  "1.0.0"
        PROVIDES "thx_mock.ServiceA"
    )

    # IPlugin-based mock that registers ServiceA in onLoad but does NOT unregister
    # it in onUnload. Used to verify PluginManager's safety-net sweep catches
    # survivors left behind by misbehaved plugins.
    add_library(mock_plugin_forgets_unload SHARED
        test/mock_plugin_forgets_unload/mock_plugin_forgets_unload.cpp
    )
    target_link_libraries(mock_plugin_forgets_unload PRIVATE ${PROJECT_NAME})
    target_include_directories(mock_plugin_forgets_unload PRIVATE test/mock_plugin)
    thx_set_warnings(mock_plugin_forgets_unload)
    target_compile_options(mock_plugin_forgets_unload PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
    thx_plugin_manifest(mock_plugin_forgets_unload
        NAME     "thx.mock.ForgetsUnloadPlugin"
        VERSION  "1.0.0"
        PROVIDES "thx_mock.ServiceA"
    )

    add_executable(test-${PROJECT_NAME}
        test/test_umbrella_header.cpp
        test/test_service_id.cpp
        test/test_version.cpp
        test/test_service_manager.cpp
        test/test_abi_types.cpp
        test/test_library.cpp
        test/test_manifest.cpp
        test/test_plugin_manager.cpp
        test/test_diagnostics.cpp
        test/test_iplugin.cpp
        test/test_registry.cpp
        test/test_facades.cpp
    )
    target_link_libraries(test-${PROJECT_NAME} ${PROJECT_NAME} Catch2::Catch2WithMain)
    target_include_directories(test-${PROJECT_NAME} PRIVATE test/mock_plugin)
    thx_set_warnings(test-${PROJECT_NAME})
    # Provide the built mock plugin paths at compile time.
    target_compile_definitions(test-${PROJECT_NAME} PRIVATE
        THX_MOCK_PLUGIN_PATH="$<TARGET_FILE:mock_plugin>"
        THX_MOCK_BAD_ABI_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_bad_abi>"
        THX_MOCK_MULTI_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_multi>"
        THX_MOCK_BAILS_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_bails>"
        THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_requires_newer>"
        THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_bails_after_register>"
        THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_forgets_unload>"
    )
    add_dependencies(test-${PROJECT_NAME}
        mock_plugin
        mock_plugin_bad_abi
        mock_plugin_multi
        mock_plugin_bails
        mock_plugin_requires_newer
        mock_plugin_bails_after_register
        mock_plugin_forgets_unload
    )

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

    # Install smoke-test: install to a temp prefix, then configure and build a
    # minimal consumer that verifies find_package(Thorax) works end-to-end.
    if(THORAX_INSTALL AND NOT CMAKE_CROSSCOMPILING)
        set(THORAX_TEST_INSTALL_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/test_install_prefix")
        if(CMAKE_BUILD_TYPE)
            set(THORAX_TEST_CONFIG "${CMAKE_BUILD_TYPE}")
        else()
            set(THORAX_TEST_CONFIG "Release")
        endif()

        add_test(NAME install.install
            COMMAND ${CMAKE_COMMAND}
                    --install "${CMAKE_CURRENT_BINARY_DIR}"
                    --prefix  "${THORAX_TEST_INSTALL_PREFIX}"
                    --config  "${THORAX_TEST_CONFIG}"
        )
        set_tests_properties(install.install PROPERTIES
            FIXTURES_SETUP thorax_installed
        )

        add_test(NAME install.consumer_configure
            COMMAND ${CMAKE_COMMAND}
                    -S "${CMAKE_CURRENT_SOURCE_DIR}/test/consumer"
                    -B "${CMAKE_CURRENT_BINARY_DIR}/test_consumer_build"
                    "-DCMAKE_PREFIX_PATH=${THORAX_TEST_INSTALL_PREFIX}"
                    -DCMAKE_BUILD_TYPE=${THORAX_TEST_CONFIG}
        )
        set_tests_properties(install.consumer_configure PROPERTIES
            FIXTURES_REQUIRED thorax_installed
            FIXTURES_SETUP    consumer_configured
        )

        add_test(NAME install.consumer_build
            COMMAND ${CMAKE_COMMAND}
                    --build "${CMAKE_CURRENT_BINARY_DIR}/test_consumer_build"
                    --config ${THORAX_TEST_CONFIG}
        )
        set_tests_properties(install.consumer_build PROPERTIES
            FIXTURES_REQUIRED consumer_configured
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Install & Package
# ---------------------------------------------------------------------------

if(THORAX_INSTALL)
    include(CMakePackageConfigHelpers)

    # Core static library
    install(TARGETS ${PROJECT_NAME}
        EXPORT  ThoraxTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
    install(DIRECTORY include/thx/
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/thx
            FILES_MATCHING
                PATTERN "*.h"
                PATTERN "*.inl"
    )
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/include/thx/version.h"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/thx
    )

    # Plugins (optional components)
    if(THORAX_BUILD_PLUGINS)
        install(TARGETS plugin_logging plugin_io
            EXPORT  ThoraxTargets
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        )
        install(DIRECTORY plugins/logging/include/
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                FILES_MATCHING PATTERN "*.h"
        )
        install(DIRECTORY plugins/io/include/
                DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
                FILES_MATCHING PATTERN "*.h"
        )
    endif()

    # Export set → ThoraxTargets.cmake
    install(EXPORT ThoraxTargets
        FILE        ThoraxTargets.cmake
        NAMESPACE   Thorax::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorax
    )

    # ThoraxConfig.cmake + ThoraxConfigVersion.cmake
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThoraxConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ThoraxConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorax
    )
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ThoraxConfigVersion.cmake"
        VERSION       ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ThoraxConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ThoraxConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorax
    )

    # CPack binary distribution
    set(CPACK_PACKAGE_NAME                "Thorax")
    set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_VENDOR              "LuckyNeko")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++17 cross-platform plugin framework")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
        set(CPACK_RESOURCE_FILE_LICENSE   "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE.md")
    endif()
    set(CPACK_SOURCE_IGNORE_FILES         "/\\.git/" "/build/" "/thirdparty/")
    include(CPack)
endif()
