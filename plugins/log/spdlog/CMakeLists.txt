include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)
include(addspdlog)

add_library(plugin_log_spdlog SHARED
    src/spdlog_plugin.cpp
)
# spdlog is linked PRIVATE and built static — its symbols (and bundled fmt)
# stay inside the plugin and never reach the thorax ABI surface. log_interface
# brings the ILogService header (and the libthorax link).
target_link_libraries(plugin_log_spdlog PRIVATE log_interface spdlog::spdlog)
# No public header: the plugin implements the ILogService interface (from
# <thx/log/log_service.h>, owned by log_interface), it does not define its own.
thx_set_warnings(plugin_log_spdlog)
target_compile_options(plugin_log_spdlog PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_log_spdlog
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
