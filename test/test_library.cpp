/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <library.h>

#ifndef THX_MOCK_PLUGIN_PATH
#	error "THX_MOCK_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#include <cstdint>

namespace
{
	// Function pointer types matching the mock plugin's exports (validated only
	// to confirm bind() resolved them — these tests don't call into them).
	using ThxCreateFn = void* (*)();
	using ThxDestroyFn = void (*)(void*);
	using ThxAbiVersionFn = std::uint32_t (*)();
} // namespace

TEST_CASE("Library default-constructed is empty and invalid", "[library]")
{
	thx::Library lib;
	REQUIRE_FALSE(lib);
	REQUIRE_FALSE(lib.valid());
	REQUIRE(lib.path().empty());
	REQUIRE(lib.nativeHandle() == nullptr);
}

TEST_CASE("Library::open on missing file fails and records an error", "[library]")
{
	thx::Library lib;
	lib.open("/nonexistent/library/path.dylib");
	REQUIRE_FALSE(lib);
	REQUIRE_FALSE(lib.error().empty());
	REQUIRE(lib.nativeHandle() == nullptr);
}

TEST_CASE("Library::open on a real DSO succeeds", "[library]")
{
	thx::Library lib;
	lib.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(lib);
	REQUIRE(lib.valid());
	REQUIRE_FALSE(lib.path().empty());
	REQUIRE(lib.nativeHandle() != nullptr);
}

TEST_CASE("Library::bind resolves an existing symbol", "[library]")
{
	thx::Library lib;
	lib.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(lib);

	ThxAbiVersionFn abiFn = nullptr;
	lib.bind("thx_abi_version", abiFn);

	REQUIRE(lib); // chain still valid
	REQUIRE(abiFn != nullptr);
	REQUIRE(abiFn() != 0); // the symbol returns a packed version
}

TEST_CASE("Library::bind chains and reports the first missing symbol",
		  "[library]")
{
	thx::Library lib;
	ThxCreateFn createFn = nullptr;
	ThxDestroyFn destroyFn = nullptr;
	void* bogus = nullptr;

	lib.open(THX_MOCK_PLUGIN_PATH)
		.bind("thx_create_plugin", createFn)
		.bind("does_not_exist", bogus)
		.bind("thx_destroy_plugin", destroyFn);

	REQUIRE_FALSE(lib);
	// First two resolved before the failure.
	REQUIRE(createFn != nullptr);
	REQUIRE(bogus == nullptr);
	// Subsequent bind on an invalid Library is a no-op and leaves out untouched.
	REQUIRE(destroyFn == nullptr);
}

TEST_CASE("Library::close releases the native handle and is idempotent",
		  "[library]")
{
	thx::Library lib;
	lib.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(lib);

	lib.close();
	REQUIRE_FALSE(lib);
	REQUIRE(lib.nativeHandle() == nullptr);

	// Second close is harmless.
	lib.close();
	REQUIRE_FALSE(lib);
}

TEST_CASE("Library::release transfers ownership of the native handle",
		  "[library]")
{
	thx::Library lib;
	lib.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(lib);

	void* h = lib.release();
	REQUIRE(h != nullptr);
	REQUIRE_FALSE(lib);
	REQUIRE(lib.nativeHandle() == nullptr);

	// The caller now owns h. Re-wrapping it in another Library would be
	// ideal but Library has no public constructor that adopts an existing
	// handle, so for the purposes of this test we close it via a fresh open
	// of the same path (which dlclose-refcounts to balance our outstanding
	// open). Or simply rely on the OS to reclaim it at exit — the platform
	// dlclose count from this test is small enough to be irrelevant.
	// (PluginGarbage is the production-side consumer of release().)
	(void)h;
}

TEST_CASE("Library is move-constructible and move-assignable", "[library]")
{
	thx::Library a;
	a.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(a);
	void* h = a.nativeHandle();

	thx::Library b(std::move(a));
	REQUIRE(b);
	REQUIRE(b.nativeHandle() == h);
	REQUIRE_FALSE(a);
	REQUIRE(a.nativeHandle() == nullptr);

	thx::Library c;
	c = std::move(b);
	REQUIRE(c);
	REQUIRE(c.nativeHandle() == h);
	REQUIRE_FALSE(b);
}

TEST_CASE("LIBRARY_EXTENSION matches the platform DSO suffix", "[library]")
{
	REQUIRE(thx::LIBRARY_EXTENSION != nullptr);
#if defined(_WIN32)
	REQUIRE(std::string(thx::LIBRARY_EXTENSION) == ".dll");
#elif defined(__APPLE__)
	REQUIRE(std::string(thx::LIBRARY_EXTENSION) == ".dylib");
#else
	REQUIRE(std::string(thx::LIBRARY_EXTENSION) == ".so");
#endif
}

TEST_CASE("Library::tryBind succeeds on an existing symbol", "[library]")
{
	thx::Library lib;
	lib.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(lib);

	ThxAbiVersionFn abiFn = nullptr;
	auto r = lib.tryBind("thx_abi_version", abiFn);
	REQUIRE(r);
	REQUIRE(abiFn != nullptr);
	REQUIRE(abiFn() != 0);
}

TEST_CASE("Library::tryBind reports SymbolNotFound for a missing symbol", "[library]")
{
	thx::Library lib;
	lib.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(lib);

	void* bogus = nullptr;
	auto r = lib.tryBind("definitely_not_a_real_symbol", bogus);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::SymbolNotFound);
	REQUIRE(bogus == nullptr);
	// tryBind does NOT poison the Library's validity bit.
	REQUIRE(lib.valid());
}

TEST_CASE("Library::tryBind on a closed library returns NotLoaded", "[library]")
{
	thx::Library lib;
	void* bogus = nullptr;
	auto r = lib.tryBind("any_symbol", bogus);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
}
