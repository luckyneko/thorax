# Provides the spdlog::spdlog target, used by the in-tree logging plugin.
#
# Mirrors cmake/addcatch2.cmake: prefer a system package, otherwise download
# and vendor a pinned release into thirdparty/ (git-ignored) on demand. spdlog
# bundles its own fmt, so no external dependency is pulled in. Linked PRIVATE
# into the plugin only — its symbols never reach the thorax ABI surface.

find_package(spdlog CONFIG QUIET)

if (${spdlog_FOUND})
else ()
	set(SPDLOG_VER "1.14.1")
	if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/spdlog-${SPDLOG_VER}.tar.gz")
		message(STATUS "Downloading spdlog (${SPDLOG_VER})")
		file(DOWNLOAD
			"https://github.com/gabime/spdlog/archive/v${SPDLOG_VER}.tar.gz"
			"${CMAKE_SOURCE_DIR}/thirdparty/spdlog-${SPDLOG_VER}.tar.gz"
		)
	endif()

	if(NOT EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/spdlog-${SPDLOG_VER}")
		message(STATUS "Decompress spdlog (${SPDLOG_VER})")
		execute_process(COMMAND
			${CMAKE_COMMAND} -E tar xfz "${CMAKE_SOURCE_DIR}/thirdparty/spdlog-${SPDLOG_VER}.tar.gz"
			WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/thirdparty"
		)
	endif()

	message(STATUS "Using thirdparty/spdlog (${SPDLOG_VER})")
	# Static (default) — keeps spdlog/fmt out of any shared export table.
	# Don't install or test it as part of the thorax build.
	set(SPDLOG_INSTALL OFF CACHE BOOL "Disable spdlog install" FORCE)
	set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
	set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)
	# Guard against double-add: this helper may be include()d from more than one
	# subdirectory. An explicit binary dir is required because the source lives
	# outside the including directory's tree.
	if(NOT TARGET spdlog)
		add_subdirectory(
			${CMAKE_SOURCE_DIR}/thirdparty/spdlog-${SPDLOG_VER}
			${CMAKE_BINARY_DIR}/thirdparty/spdlog-${SPDLOG_VER}
		)
	endif()

	if(TARGET spdlog)
		# Re-expose spdlog's headers as SYSTEM so the plugin's
		# -Werror/-Wall/-Wextra (and /WX /W4) don't fire on third-party headers.
		get_target_property(_spdlog_inc spdlog INTERFACE_INCLUDE_DIRECTORIES)
		if(_spdlog_inc)
			set_target_properties(spdlog PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "")
			target_include_directories(spdlog SYSTEM INTERFACE ${_spdlog_inc})
		endif()

		# spdlog is linked into a shared plugin DSO â it must be PIC.
		set_target_properties(spdlog PROPERTIES POSITION_INDEPENDENT_CODE ON)

		# Compile spdlog (and its bundled fmt) with hidden visibility so its
		# symbols — including weak template instantiations — are NOT re-exported
		# by the consuming plugin DSO. The plugin's three thx_* entry points are
		# forced default-visibility by THX_PLUGIN_API, so they still export.
		if(NOT MSVC)
			target_compile_options(spdlog PRIVATE -fvisibility=hidden -fvisibility-inlines-hidden)
		endif()
	endif()
endif ()
