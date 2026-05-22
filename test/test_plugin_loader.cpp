/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/plugin_loader.h>
#include <thx/result.h>
#include "mock_plugin.h"

#include <filesystem>
#include <fstream>

#ifndef THX_MOCK_PLUGIN_PATH
#  error "THX_MOCK_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_BAD_ABI_PLUGIN_PATH
#  error "THX_MOCK_BAD_ABI_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_MULTI_PLUGIN_PATH
#  error "THX_MOCK_MULTI_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_BAILS_PLUGIN_PATH
#  error "THX_MOCK_BAILS_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH
#  error "THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH
#  error "THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH
#  error "THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// ---------------------------------------------------------------------------
// Result<T, Error>
// ---------------------------------------------------------------------------

TEST_CASE("Result<int> - ok", "[result]")
{
	auto r = thx::Result<int>::ok(42);
	REQUIRE(r.is_ok());
	REQUIRE(!r.is_err());
	REQUIRE(bool(r));
	REQUIRE(r.value() == 42);
}

TEST_CASE("Result<int> - err", "[result]")
{
	auto r = thx::Result<int>::err({thx::ErrorCode::NotLoaded, "nope"});
	REQUIRE(r.is_err());
	REQUIRE(!r.is_ok());
	REQUIRE(!bool(r));
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE(r.error().message == "nope");
}

TEST_CASE("Result<void> - ok", "[result]")
{
	auto r = thx::Result<void>::ok();
	REQUIRE(r.is_ok());
	REQUIRE(bool(r));
}

TEST_CASE("Result<void> - err", "[result]")
{
	auto r = thx::Result<void>::err({thx::ErrorCode::FileNotFound, "missing"});
	REQUIRE(r.is_err());
	REQUIRE(!bool(r));
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

// ---------------------------------------------------------------------------
// PluginHandle
// ---------------------------------------------------------------------------

TEST_CASE("PluginHandle - default is empty", "[plugin_handle]")
{
	thx::PluginHandle h;
	REQUIRE(!bool(h));
}

TEST_CASE("PluginHandle::open - missing file returns FileNotFound", "[plugin_handle]")
{
	auto r = thx::PluginHandle::open("/nonexistent/path/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginHandle::open - valid mock plugin", "[plugin_handle]")
{
	auto r = thx::PluginHandle::open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(r.is_ok());
	REQUIRE(bool(r.value()));
	REQUIRE(r.value().create_fn()  != nullptr);
	REQUIRE(r.value().destroy_fn() != nullptr);
}

TEST_CASE("PluginHandle::open - mismatched ABI version returns VersionMismatch", "[plugin_handle]")
{
	auto r = thx::PluginHandle::open(THX_MOCK_BAD_ABI_PLUGIN_PATH);
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::VersionMismatch);

	// The diagnostic should include both the plugin's claimed version (99.0.0
	// per the mock) and the host's THORAX_VERSION so the user can tell which
	// side is too new.
	REQUIRE(r.error().message.find("99.0.0") != std::string::npos);
}

// ---------------------------------------------------------------------------
// PluginLoader — error paths
// ---------------------------------------------------------------------------

TEST_CASE("PluginLoader::load - missing file returns error", "[plugin_loader]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto r = loader.load("/nonexistent/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginLoader::unload - not-loaded path returns error", "[plugin_loader]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto r = loader.unload("/nonexistent/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
}

TEST_CASE("PluginLoader::is_loaded - false before load", "[plugin_loader]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	REQUIRE(!loader.is_loaded(THX_MOCK_PLUGIN_PATH));
}

// ---------------------------------------------------------------------------
// PluginLoader — integration with mock plugin
// ---------------------------------------------------------------------------

TEST_CASE("PluginLoader - load registers service", "[plugin_loader][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.is_loaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginLoader - loaded service is functional", "[plugin_loader][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);

	auto svc = sm.get_service<thx_mock::MockService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->ping() == 42);
}

TEST_CASE("PluginLoader - double-load same path is a no-op", "[plugin_loader][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH)); // second load: ok, no duplicate service

	// Only one registration was made; a single unload fully removes the service.
	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::MockService>() == nullptr);
}

TEST_CASE("PluginLoader - unload removes service", "[plugin_loader][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);

	// Release the service handle before unloading so the deleter fires while
	// the DSO is still open.
	{ auto svc = sm.get_service<thx_mock::MockService>(); (void)svc; }

	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(!loader.is_loaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::MockService>() == nullptr);
}

TEST_CASE("PluginLoader - destructor unloads remaining plugins",
          "[plugin_loader][integration]")
{
	thx::ServiceManager sm;
	{
		thx::PluginLoader loader(sm);
		loader.load(THX_MOCK_PLUGIN_PATH);
		REQUIRE(sm.get_service<thx_mock::MockService>() != nullptr);
	} // loader destroyed here — should unregister the service

	REQUIRE(sm.get_service<thx_mock::MockService>() == nullptr);
}

// ---------------------------------------------------------------------------
// PluginLoader — DSO keep-alive lifetime (Milestone 8b)
// ---------------------------------------------------------------------------

TEST_CASE("PluginLoader - service survives unload until collect_plugin_garbage",
          "[plugin_loader][lifetime][integration]")
{
	// Drain anything queued from previous tests so our count is meaningful.
	thx::collect_plugin_garbage();

	thx::ServiceManager sm;
	std::shared_ptr<thx_mock::MockService> svc;
	{
		thx::PluginLoader loader(sm);
		REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
		svc = sm.get_service<thx_mock::MockService>();
		REQUIRE(svc != nullptr);
		REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));

		// DSO must still be mapped — a virtual call into plugin code works.
		REQUIRE(svc->ping() == 42);
		REQUIRE(thx::pending_plugin_garbage() >= 1);
	} // loader destroyed; DSO still queued

	// Service still works after the loader is gone.
	REQUIRE(svc->ping() == 42);

	// Drop the last reference; ~MockServiceImpl runs in the still-mapped DSO.
	svc.reset();

	// Nothing has actually been unmapped yet.
	REQUIRE(thx::pending_plugin_garbage() >= 1);

	auto closed = thx::collect_plugin_garbage();
	REQUIRE(closed >= 1);
	REQUIRE(thx::pending_plugin_garbage() == 0);
}

TEST_CASE("PluginLoader - load drains the deferred-close queue",
          "[plugin_loader][lifetime][integration]")
{
	thx::collect_plugin_garbage();

	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::pending_plugin_garbage() >= 1);

	// Loading any plugin path drains the queue first.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::pending_plugin_garbage() == 0);

	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	thx::collect_plugin_garbage();
}

// ---------------------------------------------------------------------------
// PluginLoader::discover
// ---------------------------------------------------------------------------

TEST_CASE("PluginLoader::discover - finds platform-extension files only",
          "[plugin_loader][discover]")
{
	namespace fs = std::filesystem;

	// Create a temp directory with mixed file types.
	auto tmp = fs::temp_directory_path() / "thx_test_discover";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	// Create one file for each supported extension and one unrelated file.
	std::ofstream{(tmp / ("plugin_a" + std::string(thx::kPluginExtension))).string()};
	std::ofstream{(tmp / ("plugin_b" + std::string(thx::kPluginExtension))).string()};
	std::ofstream{(tmp / "readme.txt").string()};
	std::ofstream{(tmp / "data.bin").string()};

	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto found = loader.discover(tmp.string());

	fs::remove_all(tmp); // cleanup before assertions so temp files don't linger

	REQUIRE(found.size() == 2);
}

TEST_CASE("PluginLoader::discover - empty directory returns empty list",
          "[plugin_loader][discover]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_empty";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto found = loader.discover(tmp.string());

	fs::remove_all(tmp);

	REQUIRE(found.empty());
}

// ---------------------------------------------------------------------------
// PluginLoader — IPlugin ABI integration
// ---------------------------------------------------------------------------

TEST_CASE("PluginLoader - IPlugin plugin registers multiple services",
          "[plugin_loader][iplugin][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	// mock_plugin_multi requires MockService, so load mock_plugin first.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_MULTI_PLUGIN_PATH));

	REQUIRE(sm.get_service<thx_mock::MockService>() != nullptr);
	REQUIRE(sm.get_service<thx_mock::ServiceA>()    != nullptr);
	REQUIRE(sm.get_service<thx_mock::ServiceB>()    != nullptr);
}

TEST_CASE("PluginLoader - unloading IPlugin plugin removes all its services",
          "[plugin_loader][iplugin][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);
	loader.load(THX_MOCK_MULTI_PLUGIN_PATH);

	REQUIRE(loader.unload(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::ServiceA>() == nullptr);
	REQUIRE(sm.get_service<thx_mock::ServiceB>() == nullptr);
	// MockService still registered by mock_plugin.
	REQUIRE(sm.get_service<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginLoader - IPlugin onLoad returning false fails the load",
          "[plugin_loader][iplugin][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto r = loader.load(THX_MOCK_BAILS_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::RegistrationFailed);
	REQUIRE_FALSE(loader.is_loaded(THX_MOCK_BAILS_PLUGIN_PATH));
}

TEST_CASE("PluginLoader - onLoad partial registration is rolled back on failure",
          "[plugin_loader][iplugin][integration]")
{
	// mock_plugin_bails_after_register registers ServiceA in onLoad then
	// returns false. The loader must unregister ServiceA so the failed
	// load leaves the registry as it was.
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	REQUIRE(sm.list_services().empty());

	auto r = loader.load(THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::RegistrationFailed);
	REQUIRE(sm.get_service<thx_mock::ServiceA>() == nullptr);
	REQUIRE(sm.list_services().empty());
	REQUIRE_FALSE(loader.is_loaded(THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH));
}

TEST_CASE("PluginLoader - unload sweeps services left behind by onUnload",
          "[plugin_loader][iplugin][integration]")
{
	// mock_plugin_forgets_unload registers ServiceA in onLoad but does NOT
	// unregister it in onUnload. The loader's safety-net sweep must catch
	// the survivor and unregister it.
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	REQUIRE(loader.load(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::ServiceA>() != nullptr);

	REQUIRE(loader.unload(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::ServiceA>() == nullptr);
}

TEST_CASE("PluginLoader - destructor sweeps services left behind by onUnload",
          "[plugin_loader][iplugin][integration]")
{
	thx::ServiceManager sm;
	{
		thx::PluginLoader loader(sm);
		REQUIRE(loader.load(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
		REQUIRE(sm.get_service<thx_mock::ServiceA>() != nullptr);
	} // ~PluginLoader runs onUnload (no-op) then sweeps survivors.

	REQUIRE(sm.get_service<thx_mock::ServiceA>() == nullptr);
}

TEST_CASE("PluginLoader - IPlugin required() rejected when registered version is too old",
          "[plugin_loader][iplugin][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	// mock_plugin registers MockService 1.0.0;
	// mock_plugin_requires_newer demands MockService >= 2.0.0.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	auto r = loader.load(THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::VersionMismatch);
	REQUIRE(r.error().message.find("2.0.0") != std::string::npos);
	REQUIRE(r.error().message.find("1.0.0") != std::string::npos);
	REQUIRE_FALSE(loader.is_loaded(THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH));
}

TEST_CASE("PluginLoader - IPlugin required() service missing fails the load",
          "[plugin_loader][iplugin][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	// mock_plugin_multi requires MockService — without loading mock_plugin
	// first, the load must fail.
	auto r = loader.load(THX_MOCK_MULTI_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE_FALSE(loader.is_loaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

TEST_CASE("PluginLoader::discover_and_load - loads real plugin from directory",
          "[plugin_loader][discover][integration]")
{
	namespace fs = std::filesystem;

	// Create a temp directory containing a symlink (or copy) of the mock plugin.
	auto tmp     = fs::temp_directory_path() / "thx_test_discover_load";
	auto src     = fs::path(THX_MOCK_PLUGIN_PATH);
	auto dst     = tmp / src.filename();

	fs::remove_all(tmp);
	fs::create_directories(tmp);
	fs::copy_file(src, dst);

	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto summary = loader.discover_and_load(tmp.string());
	auto svc     = sm.get_service<thx_mock::MockService>();

	bool loaded_ok = (summary.loaded.size() == 1) && summary.failed.empty();
	bool svc_ok    = (svc != nullptr);
	bool ping_ok   = svc_ok && (svc->ping() == 42);

	// On Windows a loaded DLL's file is locked until FreeLibrary; unload the
	// plugin (and release the service handle) before removing the temp dir.
	// PluginLoader::unload defers the actual dlclose, so we must drain the
	// graveyard explicitly before the file is unlinked.
	svc.reset();
	loader.unload(dst.string());
	thx::collect_plugin_garbage();

	fs::remove_all(tmp);

	REQUIRE(loaded_ok);
	REQUIRE(svc_ok);
	REQUIRE(ping_ok);
}

// ---------------------------------------------------------------------------
// PluginLoader::open / load(OpenedPlugin) — three-step flow
// ---------------------------------------------------------------------------

TEST_CASE("PluginLoader::open - exposes plugin metadata without registering services",
          "[plugin_loader][open][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto opened = loader.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(opened);
	REQUIRE(bool(opened.value()));

	// Metadata is readable before the plugin is loaded.
	REQUIRE(std::string(static_cast<std::string_view>(opened.value().name())) == "thx_mock.MockService");

	// Nothing was registered — open() does not call onLoad.
	REQUIRE(sm.get_service<thx_mock::MockService>() == nullptr);
	REQUIRE_FALSE(loader.is_loaded(THX_MOCK_PLUGIN_PATH));
}

TEST_CASE("PluginLoader::open - then load(OpenedPlugin) registers the service",
          "[plugin_loader][open][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto opened = loader.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(opened);

	auto r = loader.load(std::move(opened.value()));
	REQUIRE(r);
	REQUIRE(loader.is_loaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginLoader::open - missing file returns FileNotFound",
          "[plugin_loader][open]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	auto r = loader.open("/nonexistent/plugin.dylib");
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginLoader::open - already-loaded path returns AlreadyLoaded",
          "[plugin_loader][open][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	auto r = loader.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::AlreadyLoaded);
}

TEST_CASE("PluginLoader - dropping OpenedPlugin without loading releases the DSO",
          "[plugin_loader][open][lifetime][integration]")
{
	thx::collect_plugin_garbage();

	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	{
		auto opened = loader.open(THX_MOCK_PLUGIN_PATH);
		REQUIRE(opened);
		// Drop without calling load(): destructor releases the DSO to the graveyard.
	}

	REQUIRE(thx::pending_plugin_garbage() >= 1);
	REQUIRE_FALSE(loader.is_loaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.get_service<thx_mock::MockService>() == nullptr);

	REQUIRE(thx::collect_plugin_garbage() >= 1);
}

TEST_CASE("PluginLoader::load(OpenedPlugin) - still checks required() at load time",
          "[plugin_loader][open][iplugin][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	// mock_plugin_multi requires MockService. Open it (no requirement check
	// yet), then attempt to load without first providing MockService.
	auto opened = loader.open(THX_MOCK_MULTI_PLUGIN_PATH);
	REQUIRE(opened);

	auto r = loader.load(std::move(opened.value()));
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE_FALSE(loader.is_loaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

TEST_CASE("PluginLoader - inspect required(), then load in dependency order",
          "[plugin_loader][open][integration]")
{
	thx::ServiceManager sm;
	thx::PluginLoader   loader(sm);

	// Open both plugins up front to inspect their requirements.
	auto multi_opened = loader.open(THX_MOCK_MULTI_PLUGIN_PATH);
	auto base_opened  = loader.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(multi_opened);
	REQUIRE(base_opened);

	REQUIRE(multi_opened.value().required().size() == 1);
	REQUIRE(base_opened.value().required().size() == 0);

	// Load base first (it provides MockService), then the multi plugin.
	REQUIRE(loader.load(std::move(base_opened.value())));
	REQUIRE(loader.load(std::move(multi_opened.value())));

	REQUIRE(loader.is_loaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.is_loaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

TEST_CASE("PluginLoader::check_requirements - dry-run against the registry",
          "[plugin_loader][open]")
{
	thx::ServiceManager sm;

	// Empty requirements always succeed.
	thx::Span<const thx::ServiceRequirement> empty;
	REQUIRE(thx::PluginLoader::check_requirements(sm, empty));

	// Missing service: NotLoaded.
	thx::ServiceRequirement reqs[] = {
	    {thx_mock::MockService::static_id(), thx::Version{1, 0, 0}},
	};
	auto miss = thx::PluginLoader::check_requirements(sm, {reqs, 1});
	REQUIRE_FALSE(miss);
	REQUIRE(miss.error().code == thx::ErrorCode::NotLoaded);

	// Register the service, then satisfied.
	thx::PluginLoader loader(sm);
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	REQUIRE(thx::PluginLoader::check_requirements(sm, {reqs, 1}));

	// Demanding a too-new version: VersionMismatch.
	thx::ServiceRequirement too_new[] = {
	    {thx_mock::MockService::static_id(), thx::Version{2, 0, 0}},
	};
	auto vm = thx::PluginLoader::check_requirements(sm, {too_new, 1});
	REQUIRE_FALSE(vm);
	REQUIRE(vm.error().code == thx::ErrorCode::VersionMismatch);
}
