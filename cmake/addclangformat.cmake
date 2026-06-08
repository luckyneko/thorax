# Pinned clang-format for the `format` / `format-check` targets.
#
# Mirrors the addcatch2 / addspdlog / addhttplib vendoring pattern, but for a
# *tool* rather than a library: it pulls ONE pinned, prebuilt clang-format
# binary on demand into thirdparty/ (git-ignored) so every contributor and CI
# job formats with the exact same version. clang-format output drifts between
# releases, so pinning is what makes `format-check` a meaningful gate rather
# than a "which clang-format do you have installed?" lottery.
#
# Binaries come from muttleyxd/clang-tools-static-binaries — static, prebuilt
# clang-format for linux-amd64 / macosx-amd64 / macos-arm-arm64 / windows-amd64,
# pinned by the immutable release tag + major version below. (That repo tops out
# at v20; the .clang-format options we use are all >= v14, so v20 applies them
# faithfully. QualifierAlignment — west const — needs >= v14.)
#
# Targets (neither is part of ALL; invoke explicitly):
#   cmake --build build --target format         # rewrite sources in place
#   cmake --build build --target format-check   # dry-run, non-zero on diff (CI)
#
# Override the pinned download with your own binary:
#   cmake -S . -B build -DTHX_CLANG_FORMAT=/usr/bin/clang-format

set(THX_CLANG_FORMAT_VERSION "20" CACHE STRING "Pinned clang-format major version")
set(_cf_tag "master-796e77c") # immutable release tag in the binaries repo

# Host platform -> published asset name. We key off the *host*, not the target:
# formatting runs on the developer's / CI machine regardless of cross-compiling.
if(CMAKE_HOST_WIN32)
	set(_cf_asset "clang-format-${THX_CLANG_FORMAT_VERSION}_windows-amd64.exe")
	set(_cf_exe "clang-format-${THX_CLANG_FORMAT_VERSION}.exe")
elseif(CMAKE_HOST_APPLE)
	if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
		set(_cf_asset "clang-format-${THX_CLANG_FORMAT_VERSION}_macos-arm-arm64")
	else()
		set(_cf_asset "clang-format-${THX_CLANG_FORMAT_VERSION}_macosx-amd64")
	endif()
	set(_cf_exe "clang-format-${THX_CLANG_FORMAT_VERSION}")
else()
	set(_cf_asset "clang-format-${THX_CLANG_FORMAT_VERSION}_linux-amd64")
	set(_cf_exe "clang-format-${THX_CLANG_FORMAT_VERSION}")
endif()

if(THX_CLANG_FORMAT)
	# Caller supplied their own binary — trust it, add a no-op fetch target so
	# the format targets' DEPENDS still resolves.
	set(_cf_bin "${THX_CLANG_FORMAT}")
	add_custom_target(clang_format_fetch)
else()
	set(_cf_bin "${CMAKE_SOURCE_DIR}/thirdparty/clang-format/${_cf_exe}")
	set(_cf_url "https://github.com/muttleyxd/clang-tools-static-binaries/releases/download/${_cf_tag}/${_cf_asset}")
	add_custom_target(clang_format_fetch
		COMMAND ${CMAKE_COMMAND} -DCF_URL=${_cf_url} -DCF_OUT=${_cf_bin}
				-P ${CMAKE_CURRENT_LIST_DIR}/clang_format_fetch.cmake
		COMMENT "Ensuring pinned clang-format ${THX_CLANG_FORMAT_VERSION} (${_cf_asset})"
		VERBATIM)
endif()

# Tracked C/C++ sources to format. version.h.in is excluded: clang-format can't
# parse its @CMAKE_VAR@ placeholders. thirdparty/ and build/ live outside these
# roots, so they're never swept.
file(GLOB_RECURSE _cf_files CONFIGURE_DEPENDS
	"${CMAKE_SOURCE_DIR}/include/*.h"
	"${CMAKE_SOURCE_DIR}/src/*.h" "${CMAKE_SOURCE_DIR}/src/*.inl" "${CMAKE_SOURCE_DIR}/src/*.cpp"
	"${CMAKE_SOURCE_DIR}/plugins/*.h" "${CMAKE_SOURCE_DIR}/plugins/*.cpp"
	"${CMAKE_SOURCE_DIR}/tools/*.h" "${CMAKE_SOURCE_DIR}/tools/*.cpp"
	"${CMAKE_SOURCE_DIR}/examples/*.h" "${CMAKE_SOURCE_DIR}/examples/*.cpp"
	"${CMAKE_SOURCE_DIR}/test/*.h" "${CMAKE_SOURCE_DIR}/test/*.inl" "${CMAKE_SOURCE_DIR}/test/*.cpp")

add_custom_target(format
	COMMAND "${_cf_bin}" -i --style=file ${_cf_files}
	DEPENDS clang_format_fetch
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Formatting sources with pinned clang-format ${THX_CLANG_FORMAT_VERSION}"
	VERBATIM)

add_custom_target(format-check
	COMMAND "${_cf_bin}" --dry-run --Werror --style=file ${_cf_files}
	DEPENDS clang_format_fetch
	WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
	COMMENT "Checking formatting with pinned clang-format ${THX_CLANG_FORMAT_VERSION}"
	VERBATIM)
