/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <library.h>
#include <plugin/plugin_handle.h>
#include <thx/result.h>

#include <string>

#ifndef THX_MOCK_PLUGIN_PATH
#  error "THX_MOCK_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_BAD_ABI_PLUGIN_PATH
#  error "THX_MOCK_BAD_ABI_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// ---------------------------------------------------------------------------
// PluginHandle — the thx_* export resolver / ABI gate on top of Library.
// White-box: PluginHandle has no public facade; it is exercised directly.
// ---------------------------------------------------------------------------

TEST_CASE("PluginHandle - default is empty", "[plugin_handle]")
{
	thx::plugin::PluginHandle h;
	REQUIRE(!bool(h));
}

TEST_CASE("PluginHandle::open - unloadable path returns OpenFailed", "[plugin_handle]")
{
	// PluginHandle::open doesn't pre-stat the file; any dlopen/LoadLibrary
	// failure (including "no such file") surfaces as OpenFailed. The actual
	// reason is in the error message. PluginManager's resolveCanonical step
	// is what distinguishes genuinely-missing paths and returns FileNotFound.
	auto r = thx::plugin::PluginHandle::open("/nonexistent/path/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::OpenFailed);
}

TEST_CASE("PluginHandle::open - valid mock plugin", "[plugin_handle]")
{
	auto r = thx::plugin::PluginHandle::open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(r.isOk());
	REQUIRE(bool(r.value()));
	REQUIRE(r.value().createFn()  != nullptr);
	REQUIRE(r.value().destroyFn() != nullptr);
}

TEST_CASE("PluginHandle::open - mismatched ABI version returns VersionMismatch", "[plugin_handle]")
{
	auto r = thx::plugin::PluginHandle::open(THX_MOCK_BAD_ABI_PLUGIN_PATH);
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::VersionMismatch);

	// The diagnostic should include both the plugin's claimed version (99.0.0
	// per the mock) and the host's THORAX_VERSION so the user can tell which
	// side is too new.
	REQUIRE(r.error().message.find("99.0.0") != std::string::npos);
}

TEST_CASE("PluginHandle::open - LoadFlags::Strict succeeds on a healthy plugin",
          "[plugin_handle]")
{
	// Strict (RTLD_NOW on POSIX) resolves every symbol at load time. A
	// well-formed plugin has no unresolved symbols and so opens fine.
	auto r = thx::plugin::PluginHandle::open(THX_MOCK_PLUGIN_PATH,
	    thx::Library::LoadFlags::Strict);
	REQUIRE(r.isOk());
}
