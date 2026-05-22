/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/library.h>
#include <thx/plugin/plugin_manager.h>
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
	REQUIRE(r.isOk());
	REQUIRE(!r.isErr());
	REQUIRE(bool(r));
	REQUIRE(r.value() == 42);
}

TEST_CASE("Result<int> - err", "[result]")
{
	auto r = thx::Result<int>::err({thx::ErrorCode::NotLoaded, "nope"});
	REQUIRE(r.isErr());
	REQUIRE(!r.isOk());
	REQUIRE(!bool(r));
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE(r.error().message == "nope");
}

TEST_CASE("Result<void> - ok", "[result]")
{
	auto r = thx::Result<void>::ok();
	REQUIRE(r.isOk());
	REQUIRE(bool(r));
}

TEST_CASE("Result<void> - err", "[result]")
{
	auto r = thx::Result<void>::err({thx::ErrorCode::FileNotFound, "missing"});
	REQUIRE(r.isErr());
	REQUIRE(!bool(r));
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

// ---------------------------------------------------------------------------
// PluginHandle
// ---------------------------------------------------------------------------

TEST_CASE("PluginHandle - default is empty", "[plugin_handle]")
{
	thx::plugin::PluginHandle h;
	REQUIRE(!bool(h));
}

TEST_CASE("PluginHandle::open - missing file returns FileNotFound", "[plugin_handle]")
{
	auto r = thx::plugin::PluginHandle::open("/nonexistent/path/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
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

// ---------------------------------------------------------------------------
// PluginManager — error paths
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager::load - missing file returns error", "[plugin_manager]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto r = loader.load("/nonexistent/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginManager::unload - not-loaded path returns error", "[plugin_manager]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto r = loader.unload("/nonexistent/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
}

TEST_CASE("PluginManager::isLoaded - false before load", "[plugin_manager]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(!loader.isLoaded(THX_MOCK_PLUGIN_PATH));
}

// ---------------------------------------------------------------------------
// PluginManager — integration with mock plugin
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager - load registers service", "[plugin_manager][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginManager - loaded service is functional", "[plugin_manager][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);

	auto svc = sm.getService<thx_mock::MockService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->ping() == 42);
}

TEST_CASE("PluginManager - double-load same path is a no-op", "[plugin_manager][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH)); // second load: ok, no duplicate service

	// Only one registration was made; a single unload fully removes the service.
	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
}

TEST_CASE("PluginManager - unload removes service", "[plugin_manager][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);

	// Release the service handle before unloading so the deleter fires while
	// the DSO is still open.
	{ auto svc = sm.getService<thx_mock::MockService>(); (void)svc; }

	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(!loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
}

TEST_CASE("PluginManager - destructor unloads remaining plugins",
          "[plugin_manager][integration]")
{
	thx::service::ServiceManager sm;
	{
		thx::plugin::PluginManager loader(sm);
		loader.load(THX_MOCK_PLUGIN_PATH);
		REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
	} // loader destroyed here — should unregister the service

	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
}

// ---------------------------------------------------------------------------
// PluginManager — DSO keep-alive lifetime (Milestone 8b)
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager - service survives unload until collectPluginGarbage",
          "[plugin_manager][lifetime][integration]")
{
	// Drain anything queued from previous tests so our count is meaningful.
	thx::collectPluginGarbage();

	thx::service::ServiceManager sm;
	std::shared_ptr<thx_mock::MockService> svc;
	{
		thx::plugin::PluginManager loader(sm);
		REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
		svc = sm.getService<thx_mock::MockService>();
		REQUIRE(svc != nullptr);
		REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));

		// DSO must still be mapped — a virtual call into plugin code works.
		REQUIRE(svc->ping() == 42);
		REQUIRE(thx::pendingPluginGarbage() >= 1);
	} // loader destroyed; DSO still queued

	// Service still works after the loader is gone.
	REQUIRE(svc->ping() == 42);

	// Drop the last reference; ~MockServiceImpl runs in the still-mapped DSO.
	svc.reset();

	// Nothing has actually been unmapped yet.
	REQUIRE(thx::pendingPluginGarbage() >= 1);

	auto closed = thx::collectPluginGarbage();
	REQUIRE(closed >= 1);
	REQUIRE(thx::pendingPluginGarbage() == 0);
}

TEST_CASE("PluginManager - load drains the deferred-close queue",
          "[plugin_manager][lifetime][integration]")
{
	thx::collectPluginGarbage();

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::pendingPluginGarbage() >= 1);

	// Loading any plugin path drains the queue first.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::pendingPluginGarbage() == 0);

	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	thx::collectPluginGarbage();
}

// ---------------------------------------------------------------------------
// PluginManager::discover
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager::discover - finds platform-extension files only",
          "[plugin_manager][discover]")
{
	namespace fs = std::filesystem;

	// Create a temp directory with mixed file types.
	auto tmp = fs::temp_directory_path() / "thx_test_discover";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	// Create one file for each supported extension and one unrelated file.
	std::ofstream{(tmp / ("plugin_a" + std::string(thx::LIBRARY_EXTENSION))).string()};
	std::ofstream{(tmp / ("plugin_b" + std::string(thx::LIBRARY_EXTENSION))).string()};
	std::ofstream{(tmp / "readme.txt").string()};
	std::ofstream{(tmp / "data.bin").string()};

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto found = loader.discover(tmp.string());

	fs::remove_all(tmp); // cleanup before assertions so temp files don't linger

	REQUIRE(found.size() == 2);
}

TEST_CASE("PluginManager::discover - empty directory returns empty list",
          "[plugin_manager][discover]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_empty";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto found = loader.discover(tmp.string());

	fs::remove_all(tmp);

	REQUIRE(found.empty());
}

// ---------------------------------------------------------------------------
// PluginManager — IPlugin ABI integration
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager - IPlugin plugin registers multiple services",
          "[plugin_manager][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	// mock_plugin_multi requires MockService, so load mock_plugin first.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_MULTI_PLUGIN_PATH));

	REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
	REQUIRE(sm.getService<thx_mock::ServiceA>()    != nullptr);
	REQUIRE(sm.getService<thx_mock::ServiceB>()    != nullptr);
}

TEST_CASE("PluginManager - unloading IPlugin plugin removes all its services",
          "[plugin_manager][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);
	loader.load(THX_MOCK_MULTI_PLUGIN_PATH);

	REQUIRE(loader.unload(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::ServiceA>() == nullptr);
	REQUIRE(sm.getService<thx_mock::ServiceB>() == nullptr);
	// MockService still registered by mock_plugin.
	REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginManager - IPlugin onLoad returning false fails the load",
          "[plugin_manager][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto r = loader.load(THX_MOCK_BAILS_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::RegistrationFailed);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_BAILS_PLUGIN_PATH));
}

TEST_CASE("PluginManager - onLoad partial registration is rolled back on failure",
          "[plugin_manager][iplugin][integration]")
{
	// mock_plugin_bails_after_register registers ServiceA in onLoad then
	// returns false. The loader must unregister ServiceA so the failed
	// load leaves the registry as it was.
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(sm.listServices().empty());

	auto r = loader.load(THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::RegistrationFailed);
	REQUIRE(sm.getService<thx_mock::ServiceA>() == nullptr);
	REQUIRE(sm.listServices().empty());
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH));
}

TEST_CASE("PluginManager - unload sweeps services left behind by onUnload",
          "[plugin_manager][iplugin][integration]")
{
	// mock_plugin_forgets_unload registers ServiceA in onLoad but does NOT
	// unregister it in onUnload. The loader's safety-net sweep must catch
	// the survivor and unregister it.
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::ServiceA>() != nullptr);

	REQUIRE(loader.unload(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::ServiceA>() == nullptr);
}

TEST_CASE("PluginManager - destructor sweeps services left behind by onUnload",
          "[plugin_manager][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	{
		thx::plugin::PluginManager loader(sm);
		REQUIRE(loader.load(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
		REQUIRE(sm.getService<thx_mock::ServiceA>() != nullptr);
	} // ~PluginManager runs onUnload (no-op) then sweeps survivors.

	REQUIRE(sm.getService<thx_mock::ServiceA>() == nullptr);
}

TEST_CASE("PluginManager - IPlugin required() rejected when registered version is too old",
          "[plugin_manager][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	// mock_plugin registers MockService 1.0.0;
	// mock_plugin_requires_newer demands MockService >= 2.0.0.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	auto r = loader.load(THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::VersionMismatch);
	REQUIRE(r.error().message.find("2.0.0") != std::string::npos);
	REQUIRE(r.error().message.find("1.0.0") != std::string::npos);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH));
}

TEST_CASE("PluginManager - IPlugin required() service missing fails the load",
          "[plugin_manager][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	// mock_plugin_multi requires MockService — without loading mock_plugin
	// first, the load must fail.
	auto r = loader.load(THX_MOCK_MULTI_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

TEST_CASE("PluginManager::discoverAndLoad - loads real plugin from directory",
          "[plugin_manager][discover][integration]")
{
	namespace fs = std::filesystem;

	// Create a temp directory containing a symlink (or copy) of the mock plugin.
	auto tmp     = fs::temp_directory_path() / "thx_test_discover_load";
	auto src     = fs::path(THX_MOCK_PLUGIN_PATH);
	auto dst     = tmp / src.filename();

	fs::remove_all(tmp);
	fs::create_directories(tmp);
	fs::copy_file(src, dst);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto summary = loader.discoverAndLoad(tmp.string());
	auto svc     = sm.getService<thx_mock::MockService>();

	bool loaded_ok = (summary.loaded.size() == 1) && summary.failed.empty();
	bool svc_ok    = (svc != nullptr);
	bool ping_ok   = svc_ok && (svc->ping() == 42);

	// On Windows a loaded DLL's file is locked until FreeLibrary; unload the
	// plugin (and release the service handle) before removing the temp dir.
	// PluginManager::unload defers the actual dlclose, so we must drain the
	// graveyard explicitly before the file is unlinked.
	svc.reset();
	loader.unload(dst.string());
	thx::collectPluginGarbage();

	fs::remove_all(tmp);

	REQUIRE(loaded_ok);
	REQUIRE(svc_ok);
	REQUIRE(ping_ok);
}

// ---------------------------------------------------------------------------
// PluginManager::open / load(OpenedPlugin) — three-step flow
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager::open - exposes plugin metadata without registering services",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto opened = loader.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(opened);
	REQUIRE(bool(opened.value()));

	// Metadata is readable before the plugin is loaded.
	REQUIRE(std::string(static_cast<std::string_view>(opened.value().name())) == "thx_mock.MockService");

	// Nothing was registered — open() does not call onLoad.
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
}

TEST_CASE("PluginManager::open - then load(OpenedPlugin) registers the service",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto opened = loader.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(opened);

	auto r = loader.load(std::move(opened.value()));
	REQUIRE(r);
	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginManager::open - missing file returns FileNotFound",
          "[plugin_manager][open]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto r = loader.open("/nonexistent/plugin.dylib");
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginManager::open - already-loaded path returns AlreadyLoaded",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	auto r = loader.open(THX_MOCK_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::AlreadyLoaded);
}

TEST_CASE("PluginManager - dropping OpenedPlugin without loading releases the DSO",
          "[plugin_manager][open][lifetime][integration]")
{
	thx::collectPluginGarbage();

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	{
		auto opened = loader.open(THX_MOCK_PLUGIN_PATH);
		REQUIRE(opened);
		// Drop without calling load(): destructor releases the DSO to the graveyard.
	}

	REQUIRE(thx::pendingPluginGarbage() >= 1);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);

	REQUIRE(thx::collectPluginGarbage() >= 1);
}

TEST_CASE("PluginManager::load(OpenedPlugin) - still checks required() at load time",
          "[plugin_manager][open][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	// mock_plugin_multi requires MockService. Open it (no requirement check
	// yet), then attempt to load without first providing MockService.
	auto opened = loader.open(THX_MOCK_MULTI_PLUGIN_PATH);
	REQUIRE(opened);

	auto r = loader.load(std::move(opened.value()));
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

TEST_CASE("PluginManager - inspect required(), then load in dependency order",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

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

	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isLoaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

TEST_CASE("PluginManager::checkRequirements - dry-run against the registry",
          "[plugin_manager][open]")
{
	thx::service::ServiceManager sm;

	// Empty requirements always succeed.
	thx::Span<const thx::plugin::ServiceRequirement> empty;
	REQUIRE(thx::plugin::PluginManager::checkRequirements(sm, empty));

	// Missing service: NotLoaded.
	thx::plugin::ServiceRequirement reqs[] = {
	    {thx_mock::MockService::staticId(), thx::Version{1, 0, 0}},
	};
	auto miss = thx::plugin::PluginManager::checkRequirements(sm, {reqs, 1});
	REQUIRE_FALSE(miss);
	REQUIRE(miss.error().code == thx::ErrorCode::NotLoaded);

	// Register the service, then satisfied.
	thx::plugin::PluginManager loader(sm);
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	REQUIRE(thx::plugin::PluginManager::checkRequirements(sm, {reqs, 1}));

	// Demanding a too-new version: VersionMismatch.
	thx::plugin::ServiceRequirement too_new[] = {
	    {thx_mock::MockService::staticId(), thx::Version{2, 0, 0}},
	};
	auto vm = thx::plugin::PluginManager::checkRequirements(sm, {too_new, 1});
	REQUIRE_FALSE(vm);
	REQUIRE(vm.error().code == thx::ErrorCode::VersionMismatch);
}
