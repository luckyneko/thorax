# On-demand fetch of the pinned clang-format binary.
#
# Invoked in script mode by the `format` / `format-check` targets:
#   cmake -DCF_URL=<url> -DCF_OUT=<path> -P clang_format_fetch.cmake
#
# Downloads only when the binary is missing, so the cost is paid once (never at
# configure time, and not on every `--target format`). Kept separate from
# addclangformat.cmake so the download runs when the target runs, not when the
# project is configured.

if(EXISTS "${CF_OUT}")
	return()
endif()

get_filename_component(_dir "${CF_OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_dir}")

message(STATUS "Downloading pinned clang-format: ${CF_URL}")
file(DOWNLOAD "${CF_URL}" "${CF_OUT}" SHOW_PROGRESS STATUS _status)

list(GET _status 0 _code)
if(NOT _code EQUAL 0)
	list(GET _status 1 _msg)
	file(REMOVE "${CF_OUT}") # don't leave a truncated/half-written file behind
	message(FATAL_ERROR "clang-format download failed (${_code}): ${_msg}\n  URL: ${CF_URL}")
endif()

if(NOT WIN32)
	execute_process(COMMAND chmod +x "${CF_OUT}")
endif()
