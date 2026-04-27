include(thx_warnings)

add_library(plugin_io SHARED
    src/io_plugin.cpp
)
target_link_libraries(plugin_io PRIVATE ${PROJECT_NAME})
target_include_directories(plugin_io PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
thx_set_warnings(plugin_io)
target_compile_options(plugin_io PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
