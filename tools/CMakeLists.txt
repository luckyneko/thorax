# Framework tools. Each tool gets its own subdirectory + CMakeLists so it can
# grow (per-tool tests, install rules, helper sources) without crowding the
# root build.

add_subdirectory(thx_emit_manifest)
