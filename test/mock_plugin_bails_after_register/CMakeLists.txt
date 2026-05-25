# IPlugin-based mock that registers ServiceA in onLoad then returns false.
# Used to verify PluginManager rolls back partial registrations on failure.
add_library(mock_plugin_bails_after_register SHARED mock_plugin_bails_after_register.cpp)
target_link_libraries(mock_plugin_bails_after_register PRIVATE mock_plugin_headers)
thx_set_warnings(mock_plugin_bails_after_register)
target_compile_options(mock_plugin_bails_after_register PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_auto_manifest(mock_plugin_bails_after_register)
