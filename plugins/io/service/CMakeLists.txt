include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)

# The io provider: registers IIoService (the scheme dispatcher). Ships no
# built-in handler — file:// lives in plugins/io/file.
add_library(plugin_io_service SHARED
    src/io_service_plugin.cpp
)
target_link_libraries(plugin_io_service PRIVATE ${PROJECT_NAME})
target_include_directories(plugin_io_service PRIVATE ${THX_IO_INCLUDE_DIR})
thx_set_warnings(plugin_io_service)
target_compile_options(plugin_io_service PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_io_service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
