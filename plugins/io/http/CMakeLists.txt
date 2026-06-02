include(GNUInstallDirs)
include(thx_warnings)
include(thx_plugin_auto_manifest)
include(addhttplib)

add_library(plugin_io_http SHARED
    src/http_plugin.cpp
)
# httplib is header-only and linked PRIVATE; compiled into this plugin's TU
# (already -fvisibility=hidden), so it never reaches the export table.
target_link_libraries(plugin_io_http PRIVATE ${PROJECT_NAME} httplib::httplib)
target_include_directories(plugin_io_http PRIVATE ${THX_IO_INCLUDE_DIR})
# No public header: the plugin implements the io IProtocol interface and
# requires the IIoService provider (both from plugins/io/service/include).
thx_set_warnings(plugin_io_http)
target_compile_options(plugin_io_http PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(plugin_io_http
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/thorax/plugins
)
