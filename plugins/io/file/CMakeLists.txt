include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)

# The file:// handler: requires the IIoService provider and contributes a
# file:// IProtocol to it.
add_library(plugin_io_file SHARED
    src/file_plugin.cpp
)
target_link_libraries(plugin_io_file PRIVATE ${PROJECT_NAME})
target_include_directories(plugin_io_file PRIVATE ${THX_IO_INCLUDE_DIR})
thx_set_warnings(plugin_io_file)
target_compile_options(plugin_io_file PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_io_file
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
