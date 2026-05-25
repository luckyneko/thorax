# IPlugin-based mock that registers ServiceA in onLoad but does NOT unregister
# it in onUnload. Used to verify PluginManager's safety-net sweep catches
# survivors left behind by misbehaved plugins.
add_library(mock_plugin_forgets_unload SHARED mock_plugin_forgets_unload.cpp)
target_link_libraries(mock_plugin_forgets_unload PRIVATE mock_plugin_headers)
thx_set_warnings(mock_plugin_forgets_unload)
target_compile_options(mock_plugin_forgets_unload PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(mock_plugin_forgets_unload)
