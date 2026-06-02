include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)

# The file:// handler: requires the IIoService provider and contributes a
# file:// IProtocol to it. io_interface brings the io headers and the libthorax
# link (no link to the provider DSO — the service is resolved at runtime).
add_library(plugin_io_file SHARED
    src/file_plugin.cpp
)
target_link_libraries(plugin_io_file PRIVATE io_interface)
thx_set_warnings(plugin_io_file)
target_compile_options(plugin_io_file PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_io_file
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
