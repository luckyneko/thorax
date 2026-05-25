# IPlugin-based mock whose onLoad returns false.
add_library(mock_plugin_bails SHARED mock_plugin_bails.cpp)
target_link_libraries(mock_plugin_bails PRIVATE ${PROJECT_NAME})
thx_set_warnings(mock_plugin_bails)
target_compile_options(mock_plugin_bails PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(mock_plugin_bails)
