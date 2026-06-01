# Install + CPack package rules for Thorax.
#
# Loaded from the root CMakeLists when THORAX_INSTALL is ON. Kept in cmake/
# (rather than reached via add_subdirectory) because install() calls reference
# targets owned by multiple dirs — the core library, plugins — and emit a
# single ThoraxTargets export set that spans all of them.

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Core static library
install(TARGETS ${PROJECT_NAME}
    EXPORT  ThoraxTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/thx/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/thx
        FILES_MATCHING
            PATTERN "*.h"
            PATTERN "*.inl"
)
install(FILES "${CMAKE_BINARY_DIR}/include/thx/version.h"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/thx
)

# Plugins (optional components)
if(THORAX_BUILD_PLUGINS)
    # The plugin DSO must sit next to its sidecar so discover() pairs them by
    # suffix swap. On Windows a SHARED lib's .dll is the RUNTIME artifact, so it
    # must target the same plugins dir as the LIBRARY (.so/.dylib) does on Unix
    # — not the global bin/, which would split the DLL from its manifest.
    install(TARGETS plugin_spdlog plugin_http
        EXPORT  ThoraxTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
        RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
    )
    # Neither in-tree plugin ships a public header: the spdlog plugin implements
    # the core thx::log::ILogService and the http plugin the core thx::io
    # IProtocol — both interfaces are installed with the library.
endif()

# Export set → ThoraxTargets.cmake
install(EXPORT ThoraxTargets
    FILE        ThoraxTargets.cmake
    NAMESPACE   Thorax::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorax
)

# ThoraxConfig.cmake + ThoraxConfigVersion.cmake
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/ThoraxConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/ThoraxConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorax
)
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/ThoraxConfigVersion.cmake"
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_BINARY_DIR}/ThoraxConfig.cmake"
    "${CMAKE_BINARY_DIR}/ThoraxConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Thorax
)

# CPack binary distribution
set(CPACK_PACKAGE_NAME                "Thorax")
set(CPACK_PACKAGE_VERSION             "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR              "LuckyNeko")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++17 cross-platform plugin framework")
if(EXISTS "${CMAKE_SOURCE_DIR}/LICENSE.md")
    set(CPACK_RESOURCE_FILE_LICENSE   "${CMAKE_SOURCE_DIR}/LICENSE.md")
endif()
set(CPACK_SOURCE_IGNORE_FILES         "/\\.git/" "/build/" "/thirdparty/")
include(CPack)
