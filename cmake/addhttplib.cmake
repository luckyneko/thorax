# Provides the httplib::httplib target (header-only cpp-httplib), used by the
# in-tree http plugin and its test.
#
# Mirrors cmake/addspdlog.cmake: prefer a system package, otherwise download and
# vendor a pinned release into thirdparty/ (git-ignored) on demand. Header-only
# and dependency-free in this configuration (no OpenSSL → http:// only). Compiled
# into the consuming plugin's TU, which is already -fvisibility=hidden, so its
# symbols never reach the plugin's export table.

find_package(httplib CONFIG QUIET)

if (${httplib_FOUND})
else ()
	set(HTTPLIB_VER "0.15.3")
	if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/cpp-httplib-${HTTPLIB_VER}.tar.gz")
		message(STATUS "Downloading cpp-httplib (${HTTPLIB_VER})")
		file(DOWNLOAD
			"https://github.com/yhirose/cpp-httplib/archive/v${HTTPLIB_VER}.tar.gz"
			"${CMAKE_SOURCE_DIR}/thirdparty/cpp-httplib-${HTTPLIB_VER}.tar.gz"
		)
	endif()

	if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/cpp-httplib-${HTTPLIB_VER}")
		message(STATUS "Decompress cpp-httplib (${HTTPLIB_VER})")
		execute_process(COMMAND
			${CMAKE_COMMAND} -E tar xfz "${CMAKE_SOURCE_DIR}/thirdparty/cpp-httplib-${HTTPLIB_VER}.tar.gz"
			WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/thirdparty"
		)
	endif()

	message(STATUS "Using thirdparty/cpp-httplib (${HTTPLIB_VER})")
	set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
	set(HTTPLIB_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)
	set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)

	if(NOT TARGET httplib)
		add_subdirectory(
			${CMAKE_SOURCE_DIR}/thirdparty/cpp-httplib-${HTTPLIB_VER}
			${CMAKE_BINARY_DIR}/thirdparty/cpp-httplib-${HTTPLIB_VER}
		)
	endif()

	# Re-expose httplib's headers as SYSTEM so the plugin's -Werror/-Wall/-Wextra
	# (and /WX /W4) don't fire on the third-party header.
	if(TARGET httplib)
		get_target_property(_httplib_inc httplib INTERFACE_INCLUDE_DIRECTORIES)
		if(_httplib_inc)
			set_target_properties(httplib PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
			target_include_directories(httplib SYSTEM INTERFACE ${_httplib_inc})
		endif()
	endif()
endif ()
