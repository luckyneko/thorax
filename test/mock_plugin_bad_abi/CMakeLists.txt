# Mock plugin that exports thx_abi_version() = 99.0.0 to test version-mismatch
# rejection. Hand-written manifest: thx_emit_manifest can't introspect a DSO
# it can't open, so this is the canonical case for thx_plugin_manifest().
add_library(mock_plugin_bad_abi SHARED mock_bad_abi.cpp)
target_link_libraries(mock_plugin_bad_abi PRIVATE ${PROJECT_NAME})
thx_set_warnings(mock_plugin_bad_abi)
target_compile_options(mock_plugin_bad_abi PRIVATE $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>)
thx_plugin_manifest(mock_plugin_bad_abi
    NAME    "test.BadAbi"
    VERSION "1.0.0"
)
