# IPlugin-based mock that requires MockService at 2.0.0 (mock_plugin only
# registers 1.0.0), used to test the versioned-requirement rejection path.
add_library(mock_plugin_requires_newer SHARED mock_plugin_requires_newer.cpp)
target_link_libraries(mock_plugin_requires_newer PRIVATE mock_plugin_headers)
thx_set_warnings(mock_plugin_requires_newer)
target_compile_options(mock_plugin_requires_newer PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(mock_plugin_requires_newer)
