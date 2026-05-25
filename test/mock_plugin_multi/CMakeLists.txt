# IPlugin-based mock that registers ServiceA + ServiceB and requires MockService.
add_library(mock_plugin_multi SHARED mock_plugin_multi.cpp)
target_link_libraries(mock_plugin_multi PRIVATE mock_plugin_headers)
thx_set_warnings(mock_plugin_multi)
target_compile_options(mock_plugin_multi PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(mock_plugin_multi)
