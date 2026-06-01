include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)
include(addspdlog)

add_library(plugin_spdlog SHARED
    src/spdlog_plugin.cpp
)
# spdlog is linked PRIVATE and built static — its symbols (and bundled fmt)
# stay inside the plugin and never reach the thorax ABI surface.
target_link_libraries(plugin_spdlog PRIVATE ${PROJECT_NAME} spdlog::spdlog)
# No public header: the plugin implements the core thx::log::ILogService
# interface (from <thx/log/log_service.h>), it does not define its own.
thx_set_warnings(plugin_spdlog)
target_compile_options(plugin_spdlog PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_spdlog
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
