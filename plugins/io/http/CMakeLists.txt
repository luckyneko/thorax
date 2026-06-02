include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)
include(addhttplib)

add_library(plugin_http SHARED
    src/http_plugin.cpp
)
# httplib is header-only and linked PRIVATE; compiled into this plugin's TU
# (already -fvisibility=hidden), so it never reaches the export table.
target_link_libraries(plugin_http PRIVATE ${PROJECT_NAME} httplib::httplib)
# No public header: the plugin implements the core thx::io::IProtocol interface.
thx_set_warnings(plugin_http)
target_compile_options(plugin_http PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_http
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
