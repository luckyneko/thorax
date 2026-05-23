include(thx_warnings)
include(thx_plugin_manifest)

add_library(plugin_logging SHARED
    src/logging_plugin.cpp
)
target_link_libraries(plugin_logging PRIVATE ${PROJECT_NAME})
target_include_directories(plugin_logging PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
thx_set_warnings(plugin_logging)
target_compile_options(plugin_logging PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_manifest(plugin_logging
    NAME     "thx.plugins.logging.ILoggingService"
    VERSION  "1.0.0"
    PROVIDES "thx.plugins.logging.ILoggingService"
)
