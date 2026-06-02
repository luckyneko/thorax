include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)

# The io provider: registers IIoService (the scheme dispatcher). Ships no
# built-in handler — file:// lives in plugins/io/file. io_interface brings the
# io headers and the libthorax link.
add_library(plugin_io_service SHARED
    src/io_service_plugin.cpp
)
target_link_libraries(plugin_io_service PRIVATE io_interface)
thx_set_warnings(plugin_io_service)
target_compile_options(plugin_io_service PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_io_service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
