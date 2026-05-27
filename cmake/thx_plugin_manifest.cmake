# cmake/thx_plugin_manifest.cmake
# Provides thx_plugin_manifest(<target> NAME ... VERSION ... [PROVIDES ...] [REQUIRES ...]).
#
# Generates a `<basename>.thx.json` sidecar next to the DSO produced by
# <target>. The sidecar is required: PluginManager::discover() refuses to
# pick up any DSO that doesn't have one.
#
# Usage:
#   thx_plugin_manifest(my_plugin
#       NAME    "thx.cameras.AcmeCameraDriver"
#       VERSION "1.2.0"
#       PROVIDES "thx.cameras.ICameraDriver"
#                "thx.bus.IUsbDevice"
#       REQUIRES "thx.io.ILogService:1.0.0"
#                "thx.gpu.IShaderCache:2.5.1"
#       [DESTINATION <relative-dir>]   # optional: also install the sidecar
#   )
#
# REQUIRES entries use "id:version" syntax (parsed into {id, version} objects
# in the emitted JSON).
#
# The file is written via file(GENERATE) so the OUTPUT path can use
# $<TARGET_FILE_DIR:...> / $<TARGET_FILE_BASE_NAME:...> generator expressions
# and resolve to wherever CMake actually puts the DSO.
#
# If DESTINATION is given, the sidecar is added to install(FILES) at that
# path. Plugins not meant for installation just omit DESTINATION.

function(thx_plugin_manifest target)
    cmake_parse_arguments(ARG
        ""                                  # options
        "NAME;VERSION;DESTINATION"          # one-value
        "PROVIDES;REQUIRES"                 # multi-value
        ${ARGN}
    )

    if(NOT ARG_NAME)
        message(FATAL_ERROR "thx_plugin_manifest(${target}): NAME is required")
    endif()
    if(NOT ARG_VERSION)
        message(FATAL_ERROR "thx_plugin_manifest(${target}): VERSION is required")
    endif()

    # Build the provides JSON array.
    if(ARG_PROVIDES)
        set(_items "")
        foreach(_p IN LISTS ARG_PROVIDES)
            list(APPEND _items "    \"${_p}\"")
        endforeach()
        list(JOIN _items ",\n" _joined)
        set(_provides_json "[\n${_joined}\n  ]")
    else()
        set(_provides_json "[]")
    endif()

    # Build the requires JSON array. Each entry must be "id:version".
    if(ARG_REQUIRES)
        set(_items "")
        foreach(_r IN LISTS ARG_REQUIRES)
            if(NOT _r MATCHES "^([^:]+):([^:]+)$")
                message(FATAL_ERROR
                    "thx_plugin_manifest(${target}): REQUIRES entry '${_r}' "
                    "must be in 'id:version' form"
                )
            endif()
            list(APPEND _items
                "    {\"id\": \"${CMAKE_MATCH_1}\", \"version\": \"${CMAKE_MATCH_2}\"}"
            )
        endforeach()
        list(JOIN _items ",\n" _joined)
        set(_requires_json "[\n${_joined}\n  ]")
    else()
        set(_requires_json "[]")
    endif()

    set(_content
"{
  \"schema\":   1,
  \"name\":     \"${ARG_NAME}\",
  \"version\":  \"${ARG_VERSION}\",
  \"provides\": ${_provides_json},
  \"requires\": ${_requires_json}
}
")

    # OUTPUT path mirrors the DSO basename including any `lib` prefix CMake
    # adds on Unix (so the file pairs as e.g. `libmock_plugin.thx.json`).
    set(_manifest_path
        "$<TARGET_FILE_DIR:${target}>/$<TARGET_FILE_PREFIX:${target}>$<TARGET_FILE_BASE_NAME:${target}>.thx.json"
    )
    file(GENERATE
        OUTPUT  "${_manifest_path}"
        CONTENT "${_content}"
    )

    if(ARG_DESTINATION)
        install(FILES "${_manifest_path}" DESTINATION "${ARG_DESTINATION}")
    endif()
endfunction()
