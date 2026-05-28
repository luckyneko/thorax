# Testing target + integration scaffolding. Caller is expected to have already
# called enable_testing() (must be at the project root for ctest to find tests),
# loaded Catch2 (addcatch2), and entered an `if(THORAX_BUILD_TESTING)` block.

# Mock plugins — each in its own subdirectory, mirroring plugins/<name>/ and
# tools/<name>/. The shared mock_plugin_headers INTERFACE lib (defined in
# mock_plugin/) lets siblings reuse MockService/ServiceA/ServiceB without
# linking the SHARED mock_plugin DSO at runtime.
add_subdirectory(mock_plugin)
add_subdirectory(mock_plugin_bad_abi)
add_subdirectory(mock_plugin_multi)
add_subdirectory(mock_plugin_bails)
add_subdirectory(mock_plugin_requires_newer)
add_subdirectory(mock_plugin_bails_after_register)
add_subdirectory(mock_plugin_forgets_unload)

add_executable(test-${PROJECT_NAME}
    test_umbrella_header.cpp
    test_service_id.cpp
    test_version.cpp
    test_service_manager.cpp
    test_abi_types.cpp
    test_library.cpp
    test_manifest.cpp
    test_emit_manifest.cpp
    test_plugin_manager.cpp
    test_diagnostics.cpp
    test_iplugin.cpp
    test_registry.cpp
    test_facades.cpp
)
target_link_libraries(test-${PROJECT_NAME} mock_plugin_headers Catch2::Catch2WithMain)
# Tests are in-tree and may exercise private internals (Registry, PluginManager,
# Library, etc.). Add the library's private include dir so existing #include
# paths keep resolving.
target_include_directories(test-${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
# Hosts loading shared-lib thorax plugins should use hidden visibility so
# user-defined service-interface typeinfo gets the libc++abi non-unique flag.
# Both the host and the plugin then mark the typeinfo non-unique; equality
# falls back to name comparison rather than failing on address mismatch
# (each DSO ends up with its own typeinfo address under RTLD_LOCAL).
set_target_properties(test-${PROJECT_NAME} PROPERTIES
    CXX_VISIBILITY_PRESET     hidden
    VISIBILITY_INLINES_HIDDEN ON
)
thx_set_warnings(test-${PROJECT_NAME})
target_compile_definitions(test-${PROJECT_NAME} PRIVATE
    THX_MOCK_PLUGIN_PATH="$<TARGET_FILE:mock_plugin>"
    THX_MOCK_BAD_ABI_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_bad_abi>"
    THX_MOCK_MULTI_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_multi>"
    THX_MOCK_BAILS_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_bails>"
    THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_requires_newer>"
    THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_bails_after_register>"
    THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH="$<TARGET_FILE:mock_plugin_forgets_unload>"
    THX_EMIT_MANIFEST_PATH="$<TARGET_FILE:thx_emit_manifest>"
)
add_dependencies(test-${PROJECT_NAME}
    mock_plugin
    mock_plugin_bad_abi
    mock_plugin_multi
    mock_plugin_bails
    mock_plugin_requires_newer
    mock_plugin_bails_after_register
    mock_plugin_forgets_unload
    thx_emit_manifest
)

if(THORAX_BUILD_PLUGINS)
    target_sources(test-${PROJECT_NAME} PRIVATE
        test_logging_plugin.cpp
        test_io_plugin.cpp
    )
    target_include_directories(test-${PROJECT_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/plugins/logging/include
        ${CMAKE_SOURCE_DIR}/plugins/io/include
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
    set(THORAX_TEST_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/test_install_prefix")
    if(CMAKE_BUILD_TYPE)
        set(THORAX_TEST_CONFIG "${CMAKE_BUILD_TYPE}")
    else()
        set(THORAX_TEST_CONFIG "Release")
    endif()

    add_test(NAME install.install
        COMMAND ${CMAKE_COMMAND}
                --install "${CMAKE_BINARY_DIR}"
                --prefix  "${THORAX_TEST_INSTALL_PREFIX}"
                --config  "${THORAX_TEST_CONFIG}"
    )
    set_tests_properties(install.install PROPERTIES
        FIXTURES_SETUP thorax_installed
    )

    add_test(NAME install.consumer_configure
        COMMAND ${CMAKE_COMMAND}
                -S "${CMAKE_CURRENT_SOURCE_DIR}/consumer"
                -B "${CMAKE_BINARY_DIR}/test_consumer_build"
                "-DCMAKE_PREFIX_PATH=${THORAX_TEST_INSTALL_PREFIX}"
                -DCMAKE_BUILD_TYPE=${THORAX_TEST_CONFIG}
    )
    set_tests_properties(install.consumer_configure PROPERTIES
        FIXTURES_REQUIRED thorax_installed
        FIXTURES_SETUP    consumer_configured
    )

    add_test(NAME install.consumer_build
        COMMAND ${CMAKE_COMMAND}
                --build "${CMAKE_BINARY_DIR}/test_consumer_build"
                --config ${THORAX_TEST_CONFIG}
    )
    set_tests_properties(install.consumer_build PROPERTIES
        FIXTURES_REQUIRED consumer_configured
        FIXTURES_SETUP    consumer_built
    )

    # Run the plugin-aware consumer against the install prefix's plugins
    # dir. Verifies the sidecar install path: discover() should find both
    # in-tree plugins paired with their installed DSOs.
    set(_consumer_plugins_exe
        "${CMAKE_BINARY_DIR}/test_consumer_build/consumer_plugins")
    if(WIN32)
        set(_consumer_plugins_exe
            "${CMAKE_BINARY_DIR}/test_consumer_build/${THORAX_TEST_CONFIG}/consumer_plugins.exe")
    endif()
    add_test(NAME install.consumer_discover
        COMMAND ${_consumer_plugins_exe}
                "${THORAX_TEST_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/thorax/plugins"
    )
    set_tests_properties(install.consumer_discover PROPERTIES
        FIXTURES_REQUIRED consumer_built
    )
    # On Windows the consumer exe links thorax.dll, which installs to the
    # prefix's bin/. There is no rpath, so put that dir on PATH for the run.
    # The ENVIRONMENT property is itself a ';'-separated list, so the ';' that
    # separate Windows PATH entries must be escaped to survive as one value.
    if(WIN32)
        string(REPLACE ";" "\\;" _thx_escaped_path "$ENV{PATH}")
        set_tests_properties(install.consumer_discover PROPERTIES
            ENVIRONMENT "PATH=${THORAX_TEST_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\\;${_thx_escaped_path}"
        )
    endif()
endif()
