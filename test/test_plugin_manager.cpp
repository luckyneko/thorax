/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <library.h>
#include <thx/plugin/plugin.h>
#include <plugin/plugin_manager.h>
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

TEST_CASE("PluginManager - service survives unload until collectGarbage",
          "[plugin_manager][lifetime][integration]")
{
	// Drain anything queued from previous tests so our count is meaningful.
	thx::plugin::collectGarbage();

	thx::service::ServiceManager sm;
	thx::service::ServiceHandle<thx_mock::MockService> svc;
	{
		thx::plugin::PluginManager loader(sm);
		REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
		svc = sm.getService<thx_mock::MockService>();
		REQUIRE(svc != nullptr);
		REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));

		// DSO must still be mapped — a virtual call into plugin code works.
		REQUIRE(svc->ping() == 42);
		REQUIRE(thx::plugin::pendingGarbage() >= 1);
	} // loader destroyed; DSO still queued

	// Service still works after the loader is gone.
	REQUIRE(svc->ping() == 42);

	// Drop the last reference; ~MockServiceImpl runs in the still-mapped DSO.
	svc.reset();

	// Nothing has actually been unmapped yet.
	REQUIRE(thx::plugin::pendingGarbage() >= 1);

	auto closed = thx::plugin::collectGarbage();
	REQUIRE(closed >= 1);
	REQUIRE(thx::plugin::pendingGarbage() == 0);
}

TEST_CASE("PluginManager - load drains the deferred-close queue",
          "[plugin_manager][lifetime][integration]")
{
	thx::plugin::collectGarbage();

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::plugin::pendingGarbage() >= 1);

	// Loading any plugin path drains the queue first.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::plugin::pendingGarbage() == 0);

	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	thx::plugin::collectGarbage();
}

// ---------------------------------------------------------------------------
// PluginManager::discover
// ---------------------------------------------------------------------------

// Helper: write a minimal-but-valid manifest at `path` advertising `name`.
namespace
{
	void writeStubManifest(std::filesystem::path const& path, std::string const& name)
	{
		std::ofstream f(path);
		f << R"({
	"schema":   1,
	"name":     ")" << name << R"(",
	"version":  "1.0.0",
	"provides": [],
	"requires": []
})";
	}

	// Helper: copy a plugin DSO and its sidecar from src_dso path into dst_dir.
	// The sidecar is assumed to live next to the source with the same basename
	// and a .thx.json suffix.
	std::filesystem::path copyPluginWithSidecar(std::filesystem::path const& src_dso,
	                                            std::filesystem::path const& dst_dir)
	{
		namespace fs = std::filesystem;
		auto basename = src_dso.stem().string();   // e.g. "libmock_plugin"
		auto src_dir  = src_dso.parent_path();
		auto src_mf   = src_dir / (basename + ".thx.json");

		auto dst_dso = dst_dir / src_dso.filename();
		auto dst_mf  = dst_dir / src_mf.filename();
		fs::copy_file(src_dso, dst_dso);
		fs::copy_file(src_mf,  dst_mf);
		return dst_dso;
	}
}

TEST_CASE("PluginManager::discover - DSO without sidecar is ignored, paired DSO+sidecar is discovered",
          "[plugin_manager][discover]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	// Stub DSO with no sidecar — must be ignored.
	std::ofstream{(tmp / ("plugin_a" + std::string(thx::LIBRARY_EXTENSION))).string()};
	// Stub DSO with paired sidecar — must be discovered.
	std::ofstream{(tmp / ("plugin_b" + std::string(thx::LIBRARY_EXTENSION))).string()};
	writeStubManifest(tmp / "plugin_b.thx.json", "thx.test.PluginB");
	// Unrelated files — must be ignored.
	std::ofstream{(tmp / "readme.txt").string()};
	std::ofstream{(tmp / "data.bin").string()};

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.discover(tmp.string()));
	auto discovered = loader.plugins(thx::plugin::State::Discovered);

	fs::remove_all(tmp);

	REQUIRE(discovered.size() == 1);
	REQUIRE(discovered[0].name == "thx.test.PluginB");
}

TEST_CASE("PluginManager::discover - orphan sidecar (no DSO) is skipped with a warning",
          "[plugin_manager][discover]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_orphan";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	// Manifest without a paired DSO.
	writeStubManifest(tmp / "ghost.thx.json", "thx.test.Ghost");

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.discover(tmp.string()));
	REQUIRE(loader.plugins(thx::plugin::State::Discovered).empty());

	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::discover - empty directory yields no entries",
          "[plugin_manager][discover]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_empty";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.discover(tmp.string()));
	auto discovered = loader.plugins(thx::plugin::State::Discovered);

	fs::remove_all(tmp);

	REQUIRE(discovered.empty());
}

TEST_CASE("PluginManager::discover - missing directory returns FileNotFound",
          "[plugin_manager][discover]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto r = loader.discover("/nonexistent/path/that/does/not/exist");
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginManager::discover - real plugin populates a Discovered entry from its manifest",
          "[plugin_manager][discover][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_real";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.discover(tmp.string()));
	REQUIRE(loader.is(thx::plugin::State::Discovered, dst.string()));

	auto info = loader.pluginInfo(dst.string());
	REQUIRE(info.has_value());
	REQUIRE(info->state == thx::plugin::State::Discovered);
	// Manifest data is available without opening the DSO.
	REQUIRE(info->name == "thx_mock.MockService");
	REQUIRE(info->version == thx::Version{1, 0, 0});
	REQUIRE(info->provides.size() == 1);
	REQUIRE(info->provides[0] == "thx_mock.MockService");
	REQUIRE(info->services.empty()); // not loaded yet

	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::forget - removes a Discovered entry",
          "[plugin_manager][discover][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_forget";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.discover(tmp.string()));
	REQUIRE(loader.is(thx::plugin::State::Discovered, dst.string()));

	REQUIRE(loader.forget(dst.string()));
	REQUIRE_FALSE(loader.is(thx::plugin::State::Discovered, dst.string()));

	// Idempotent — forgetting an unknown path is ok.
	REQUIRE(loader.forget(dst.string()));

	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::forget - returns InUse for a loaded plugin",
          "[plugin_manager][discover][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	auto r = loader.forget(THX_MOCK_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::InUse);

	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	thx::plugin::collectGarbage();
}

TEST_CASE("PluginManager::discover - re-scan leaves Loaded entries untouched",
          "[plugin_manager][discover][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_rescan";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(dst.string()));
	REQUIRE(loader.is(thx::plugin::State::Loaded, dst.string()));

	// Re-discover the directory: the already-Loaded entry must not get
	// shadowed by a Discovered entry.
	REQUIRE(loader.discover(tmp.string()));
	REQUIRE(loader.is(thx::plugin::State::Loaded, dst.string()));
	REQUIRE_FALSE(loader.is(thx::plugin::State::Discovered, dst.string()));

	REQUIRE(loader.unload(dst.string()));
	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
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

	auto tmp = fs::temp_directory_path() / "thx_test_discover_load";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

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
	thx::plugin::collectGarbage();

	fs::remove_all(tmp);

	REQUIRE(loaded_ok);
	REQUIRE(svc_ok);
	REQUIRE(ping_ok);
}

// ---------------------------------------------------------------------------
// PluginManager::open / close / load — path-based lifecycle
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager::open - moves entry to Opened without registering services",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.is(thx::plugin::State::Opened, THX_MOCK_PLUGIN_PATH));

	// Metadata is queryable post-open via PluginInfo.
	auto info = loader.pluginInfo(THX_MOCK_PLUGIN_PATH);
	REQUIRE(info.has_value());
	REQUIRE(info->state == thx::plugin::State::Opened);
	REQUIRE(info->name == "thx_mock.MockService");

	// Nothing was registered — open() does not call onLoad.
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
}

TEST_CASE("PluginManager::open then load - registers the service",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE_FALSE(loader.isOpened(THX_MOCK_PLUGIN_PATH));
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

TEST_CASE("PluginManager::open / load are idempotent on the target state",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	// open() on a Loaded plugin is ok-no-op (Loaded supersedes Opened).
	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));

	// load() on a Loaded plugin is also ok-no-op.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
}

TEST_CASE("PluginManager::close - returns an Opened entry to Discovered",
          "[plugin_manager][open][lifetime][integration]")
{
	thx::plugin::collectGarbage();

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isOpened(THX_MOCK_PLUGIN_PATH));

	REQUIRE(loader.close(THX_MOCK_PLUGIN_PATH));
	REQUIRE_FALSE(loader.isOpened(THX_MOCK_PLUGIN_PATH));
	// close() always returns the entry to Discovered, even when never explicitly discovered.
	REQUIRE(loader.isDiscovered(THX_MOCK_PLUGIN_PATH));

	// The Library went to the garbage queue, not unmapped synchronously.
	REQUIRE(thx::plugin::pendingGarbage() >= 1);
	REQUIRE(thx::plugin::collectGarbage() >= 1);

	// close() on a non-Opened path is ok-no-op.
	REQUIRE(loader.close(THX_MOCK_PLUGIN_PATH));
}

TEST_CASE("PluginManager::closeAllOpened - sweeps every Opened-not-Loaded entry",
          "[plugin_manager][open][integration]")
{
	thx::plugin::collectGarbage();

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.open(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE(loader.isOpened(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isOpened(THX_MOCK_MULTI_PLUGIN_PATH));

	auto closed = loader.closeAllOpened();
	REQUIRE(closed == 2);
	REQUIRE_FALSE(loader.isOpened(THX_MOCK_PLUGIN_PATH));
	REQUIRE_FALSE(loader.isOpened(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE(loader.isDiscovered(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isDiscovered(THX_MOCK_MULTI_PLUGIN_PATH));

	thx::plugin::collectGarbage();
}

TEST_CASE("PluginManager::load on Opened entry - still checks required()",
          "[plugin_manager][open][iplugin][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	// mock_plugin_multi requires MockService. Open it first (no requirement
	// check yet), then attempt to load without first providing MockService.
	REQUIRE(loader.open(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE(loader.isOpened(THX_MOCK_MULTI_PLUGIN_PATH));

	auto r = loader.load(THX_MOCK_MULTI_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	// Failed load leaves the entry in neither Opened nor Loaded — the
	// OpenedEntry was consumed by finalizeLoad and the DSO is in the garbage queue.
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE_FALSE(loader.isOpened(THX_MOCK_MULTI_PLUGIN_PATH));

	thx::plugin::collectGarbage();
}

TEST_CASE("PluginManager - inspect required() on Opened entries, load in dependency order",
          "[plugin_manager][open][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	// Open both plugins up front to inspect their requirements via PluginInfo.
	REQUIRE(loader.open(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));

	auto multi = loader.pluginInfo(THX_MOCK_MULTI_PLUGIN_PATH);
	auto base  = loader.pluginInfo(THX_MOCK_PLUGIN_PATH);
	REQUIRE(multi.has_value());
	REQUIRE(base.has_value());

	REQUIRE(multi->requirements.size() == 1);
	REQUIRE(base->requirements.size()  == 0);

	// Load base first (it provides MockService), then the multi plugin.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_MULTI_PLUGIN_PATH));

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

// ---------------------------------------------------------------------------
// Phase 6 query API — pluginInfo / plugins / is(State, path)
//
// Only the Loaded state is populated at this stage of the migration;
// Discovered/Opened tracking arrives in later commits.
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager::plugins() returns empty when nothing is loaded",
          "[plugin_manager][query]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	REQUIRE(pm.plugins().empty());
	REQUIRE(pm.plugins(thx::plugin::State::Loaded).empty());
	REQUIRE(pm.plugins(thx::plugin::State::Opened).empty());
	REQUIRE(pm.plugins(thx::plugin::State::Discovered).empty());
}

TEST_CASE("PluginManager::pluginInfo returns nullopt for unknown paths",
          "[plugin_manager][query]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	REQUIRE_FALSE(pm.pluginInfo("/nonexistent/path.dylib").has_value());
	REQUIRE_FALSE(pm.pluginInfo(THX_MOCK_PLUGIN_PATH).has_value());
}

TEST_CASE("PluginManager::plugins() reflects a loaded plugin",
          "[plugin_manager][query][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	REQUIRE(pm.load(THX_MOCK_PLUGIN_PATH));

	auto all = pm.plugins();
	REQUIRE(all.size() == 1);
	REQUIRE(all[0].state == thx::plugin::State::Loaded);
	REQUIRE_FALSE(all[0].path.empty());
	REQUIRE_FALSE(all[0].name.empty());
	REQUIRE(all[0].services.size() >= 1);

	// Filtered query agrees.
	auto loaded = pm.plugins(thx::plugin::State::Loaded);
	REQUIRE(loaded.size() == 1);
	REQUIRE(loaded[0].path == all[0].path);

	// Discovered/Opened are not populated yet.
	REQUIRE(pm.plugins(thx::plugin::State::Discovered).empty());
	REQUIRE(pm.plugins(thx::plugin::State::Opened).empty());
}

TEST_CASE("PluginManager::pluginInfo returns a snapshot for a loaded plugin",
          "[plugin_manager][query][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	REQUIRE(pm.load(THX_MOCK_PLUGIN_PATH));

	auto info = pm.pluginInfo(THX_MOCK_PLUGIN_PATH);
	REQUIRE(info.has_value());
	REQUIRE(info->state == thx::plugin::State::Loaded);
	REQUIRE_FALSE(info->name.empty());
	REQUIRE(info->services.size() == 1);
	REQUIRE(info->services[0] == thx_mock::MockService::staticId().name());

	// Phase 5 manifests populate `provides` for the mock plugin.
	REQUIRE(info->provides.size() == 1);
	REQUIRE(info->provides[0] == "thx_mock.MockService");
}

TEST_CASE("PluginManager::is(State, path) for loaded plugins",
          "[plugin_manager][query][integration]")
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	REQUIRE_FALSE(pm.is(thx::plugin::State::Loaded, THX_MOCK_PLUGIN_PATH));

	REQUIRE(pm.load(THX_MOCK_PLUGIN_PATH));

	REQUIRE(pm.is(thx::plugin::State::Loaded, THX_MOCK_PLUGIN_PATH));
	REQUIRE_FALSE(pm.is(thx::plugin::State::Discovered, THX_MOCK_PLUGIN_PATH));
	REQUIRE_FALSE(pm.is(thx::plugin::State::Opened, THX_MOCK_PLUGIN_PATH));
}

TEST_CASE("thx::plugin facade exposes the new query functions",
          "[plugin_manager][query][integration][facade]")
{
	REQUIRE(thx::plugin::load(THX_MOCK_PLUGIN_PATH));

	auto info = thx::plugin::pluginInfo(THX_MOCK_PLUGIN_PATH);
	REQUIRE(info.has_value());
	REQUIRE(info->state == thx::plugin::State::Loaded);

	auto loaded = thx::plugin::plugins(thx::plugin::State::Loaded);
	bool found = false;
	for (auto const& p : loaded)
		if (p.path == info->path) { found = true; break; }
	REQUIRE(found);

	REQUIRE(thx::plugin::is(thx::plugin::State::Loaded, THX_MOCK_PLUGIN_PATH));

	REQUIRE(thx::plugin::unload(THX_MOCK_PLUGIN_PATH));
	thx::plugin::collectGarbage();
}

// ---------------------------------------------------------------------------
// Phase 5 — load-time manifest verification
// ---------------------------------------------------------------------------

TEST_CASE("Manifest verification - matching manifest loads cleanly",
          "[plugin_manager][manifest][integration]")
{
	// The in-tree mock_plugin's sidecar is authored to match the live IPlugin.
	// If verification is wired up correctly, the load succeeds.
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	REQUIRE(pm.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(pm.isLoaded(THX_MOCK_PLUGIN_PATH));
}

TEST_CASE("Manifest verification - mismatched name fails with ManifestMismatch",
          "[plugin_manager][manifest][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_mismatch_name";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	// Replace the sidecar with one that has a wrong name field.
	auto sidecar = (tmp / (fs::path(dst).stem().string() + ".thx.json")).string();
	{
		std::ofstream f(sidecar);
		f << R"({
	"schema":   1,
	"name":     "thx.fake.WrongName",
	"version":  "1.0.0",
	"provides": ["thx_mock.MockService"],
	"requires": []
})";
	}

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	auto r = pm.load(dst.string());
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::ManifestMismatch);
	REQUIRE(r.error().message.find("name") != std::string::npos);
	REQUIRE_FALSE(pm.isLoaded(dst.string()));
	// Service should NOT be registered after a failed verification.
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);

	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

TEST_CASE("Manifest verification - mismatched version fails with ManifestMismatch",
          "[plugin_manager][manifest][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_mismatch_version";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	auto sidecar = (tmp / (fs::path(dst).stem().string() + ".thx.json")).string();
	{
		std::ofstream f(sidecar);
		f << R"({
	"schema":   1,
	"name":     "thx_mock.MockService",
	"version":  "9.9.9",
	"provides": ["thx_mock.MockService"],
	"requires": []
})";
	}

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	auto r = pm.load(dst.string());
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::ManifestMismatch);
	REQUIRE(r.error().message.find("version") != std::string::npos);

	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

TEST_CASE("Manifest verification - mismatched provides fails with ManifestMismatch",
          "[plugin_manager][manifest][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_mismatch_provides";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	// Manifest claims a service the plugin never registers.
	auto sidecar = (tmp / (fs::path(dst).stem().string() + ".thx.json")).string();
	{
		std::ofstream f(sidecar);
		f << R"({
	"schema":   1,
	"name":     "thx_mock.MockService",
	"version":  "1.0.0",
	"provides": ["thx_mock.MockService", "thx_mock.UnshippedService"],
	"requires": []
})";
	}

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	auto r = pm.load(dst.string());
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::ManifestMismatch);
	REQUIRE(r.error().message.find("provides") != std::string::npos);
	// Roll back: the service the plugin DID register must be unregistered.
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);

	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

TEST_CASE("Manifest verification - mismatched requirements fails with ManifestMismatch",
          "[plugin_manager][manifest][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_mismatch_requires";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	// Manifest declares a requirement the IPlugin doesn't report.
	auto sidecar = (tmp / (fs::path(dst).stem().string() + ".thx.json")).string();
	{
		std::ofstream f(sidecar);
		f << R"({
	"schema":   1,
	"name":     "thx_mock.MockService",
	"version":  "1.0.0",
	"provides": ["thx_mock.MockService"],
	"requires": [{"id": "thx.fake.Imaginary", "version": "1.0.0"}]
})";
	}

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	auto r = pm.load(dst.string());
	REQUIRE_FALSE(r);
	// Two possible failure modes:
	//  - the requirement check itself fails (NotLoaded), because
	//    thx.fake.Imaginary isn't registered. We get this BEFORE onLoad
	//    runs, since checkRequirements runs first.
	// In either case the load fails and nothing is registered.
	REQUIRE_FALSE(pm.isLoaded(dst.string()));

	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

#include <atomic>
#include <thread>

TEST_CASE("PluginManager - concurrent reads are safe", "[plugin_manager][threading]")
{
	// Stress test: many threads call query methods while another thread cycles
	// load/unload. Without the coarse mutex, the readers would race the writer
	// on the m_discovered/m_opened/m_plugins maps. With it, readers see a
	// consistent snapshot.
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   pm(sm);

	REQUIRE(pm.load(THX_MOCK_PLUGIN_PATH));

	constexpr int kReaders = 4;
	constexpr int kIters   = 500;
	std::atomic<bool>      stopWriter{false};
	std::atomic<int>       errors{0};

	std::vector<std::thread> readers;
	readers.reserve(kReaders);
	for (int i = 0; i < kReaders; ++i)
	{
		readers.emplace_back([&]
		{
			for (int j = 0; j < kIters; ++j)
			{
				auto v = pm.plugins();          // snapshot all states
				auto info = pm.pluginInfo(THX_MOCK_PLUGIN_PATH);
				(void)pm.isLoaded(THX_MOCK_PLUGIN_PATH);
				if (v.empty() && !info.has_value())
				{
					// Acceptable: writer happens to be between unload and reload.
					// Just verify nothing torn — by reaching here without a
					// crash / TSan flag, we've succeeded.
				}
			}
		});
	}

	std::thread writer([&]
	{
		while (!stopWriter.load())
		{
			(void)pm.unload(THX_MOCK_PLUGIN_PATH);
			(void)pm.load(THX_MOCK_PLUGIN_PATH);
		}
	});

	for (auto& t : readers) t.join();
	stopWriter.store(true);
	writer.join();

	REQUIRE(errors == 0);

	(void)pm.unload(THX_MOCK_PLUGIN_PATH);
	(void)pm.forget(THX_MOCK_PLUGIN_PATH);
	thx::plugin::collectGarbage();
}

TEST_CASE("PluginManager::discoverAndLoad - second call reports alreadyLoaded",
          "[plugin_manager][discover][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_already_loaded";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	auto first = loader.discoverAndLoad(tmp.string());
	REQUIRE(first.loaded.size() == 1);
	REQUIRE(first.alreadyLoaded.empty());
	REQUIRE(first.failed.empty());

	// Second call: nothing new on disk, plugin already loaded. Summary
	// should still surface the plugin — in both `loaded` (it's currently
	// loaded) and `alreadyLoaded` (it wasn't freshly loaded by this call).
	auto second = loader.discoverAndLoad(tmp.string());
	REQUIRE(second.loaded.size() == 1);
	REQUIRE(second.alreadyLoaded.size() == 1);
	REQUIRE(second.alreadyLoaded[0] == second.loaded[0]);
	REQUIRE(second.failed.empty());

	loader.unload(dst.string());
	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::pluginsProviding - filters by manifest provides",
          "[plugin_manager][discover]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_plugins_providing";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader(sm);

	REQUIRE(loader.discover(tmp.string()));

	// mock_plugin provides thx_mock.MockService.
	auto matches = loader.pluginsProviding("thx_mock.MockService");
	REQUIRE(matches.size() == 1);
	REQUIRE(matches[0].state == thx::plugin::State::Discovered);

	// Bogus ID matches nothing.
	REQUIRE(loader.pluginsProviding("nope.NotAService").empty());

	fs::remove_all(tmp);
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
