include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)

# The log "service" plugin: bridges core's thx::LogSink to a registered
# thx::log::ILogService. Registers no service itself; it `requires`
# thx.log.ILogService so loadWithDependencies pulls in a backend. log_interface
# brings the ILogService header and the libthorax link.
add_library(plugin_log_service SHARED
    src/log_service_plugin.cpp
)
target_link_libraries(plugin_log_service PRIVATE log_interface)
thx_set_warnings(plugin_log_service)
target_compile_options(plugin_log_service PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_log_service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
