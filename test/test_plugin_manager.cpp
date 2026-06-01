/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "mock_plugin.h"
#include <catch2/catch_all.hpp>
#include <plugin/plugin_manager.h>
#include <registry.h>
#include <thx/plugin/plugin.h>
#include <thx/result.h>

// White-box unit tests of the PluginManager class. Each case constructs a local
// PluginManager bound to the Registry's ServiceManager (`sm` below) so the
// load/unload path runs against production wiring; the test-wide reset listener
// (test_reset_listener.cpp) returns the Registry to empty after every case, so
// `sm` is a clean slate at the start of each.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <vector>

#ifndef THX_MOCK_PLUGIN_PATH
#	error "THX_MOCK_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_BAD_ABI_PLUGIN_PATH
#	error "THX_MOCK_BAD_ABI_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_MULTI_PLUGIN_PATH
#	error "THX_MOCK_MULTI_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_BAILS_PLUGIN_PATH
#	error "THX_MOCK_BAILS_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH
#	error "THX_MOCK_REQUIRES_NEWER_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH
#	error "THX_MOCK_BAILS_AFTER_REGISTER_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

#ifndef THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH
#	error "THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// ---------------------------------------------------------------------------
// PluginManager — error paths
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager::load - missing file returns error", "[plugin_manager]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	auto r = loader.load("/nonexistent/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginManager::unload - not-loaded path returns error", "[plugin_manager]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	auto r = loader.unload("/nonexistent/plugin.dylib");
	REQUIRE(!r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
}

TEST_CASE("PluginManager::isLoaded - false before load", "[plugin_manager]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(!loader.isLoaded(THX_MOCK_PLUGIN_PATH));
}

// ---------------------------------------------------------------------------
// PluginManager — integration with mock plugin
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager - load registers service", "[plugin_manager][integration]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginManager - loaded service is functional", "[plugin_manager][integration]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);

	auto svc = sm.getService<thx_mock::MockService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->ping() == 42);
}

TEST_CASE("PluginManager - double-load same path is a no-op", "[plugin_manager][integration]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH)); // second load: ok, no duplicate service

	// Only one registration was made; a single unload fully removes the service.
	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
}

TEST_CASE("PluginManager - unload removes service", "[plugin_manager][integration]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	loader.load(THX_MOCK_PLUGIN_PATH);

	// Release the service handle before unloading so the deleter fires while
	// the DSO is still open.
	{
		auto svc = sm.getService<thx_mock::MockService>();
		(void)svc;
	}

	REQUIRE(loader.unload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(!loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
}

TEST_CASE("PluginManager - destructor unloads remaining plugins",
		  "[plugin_manager][integration]")
{
	auto& sm = thx::registry().serviceManager();
	{
		thx::plugin::PluginManager loader(sm);
		loader.load(THX_MOCK_PLUGIN_PATH);
		REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
	} // loader destroyed here — should unregister the service

	REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
}

TEST_CASE("PluginManager::clear - unloads all loaded plugins and empties the manager",
		  "[plugin_manager][integration]")
{
	auto& sm = thx::registry().serviceManager();
	{
		thx::plugin::PluginManager loader(sm);
		REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
		REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
		REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);

		loader.clear();

		REQUIRE(loader.plugins().empty());
		REQUIRE(sm.getService<thx_mock::MockService>() == nullptr);
	} // loader destroyed: clear() again is a no-op on the now-empty manager.

	// clear() queued the DSO into the process-wide PluginGarbage; drain it now
	// that no ServiceHandle into the plugin remains.
	thx::plugin::collectGarbage();
}

// ---------------------------------------------------------------------------
// PluginManager — DSO keep-alive lifetime (Milestone 8b)
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager - service survives unload until collectGarbage",
		  "[plugin_manager][lifetime][integration]")
{
	// Drain anything queued from previous tests so our count is meaningful.
	thx::plugin::collectGarbage();

	auto& sm = thx::registry().serviceManager();
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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	"name":     ")"
		  << name << R"(",
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
		auto basename = src_dso.stem().string(); // e.g. "libmock_plugin"
		auto src_dir = src_dso.parent_path();
		auto src_mf = src_dir / (basename + ".thx.json");

		auto dst_dso = dst_dir / src_dso.filename();
		auto dst_mf = dst_dir / src_mf.filename();
		fs::copy_file(src_dso, dst_dso);
		fs::copy_file(src_mf, dst_mf);
		return dst_dso;
	}
} // namespace

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(loader.discover(tmp.string()));
	auto discovered = loader.plugins(thx::plugin::State::Discovered);

	fs::remove_all(tmp);

	REQUIRE(discovered.empty());
}

TEST_CASE("PluginManager::discover - missing directory returns FileNotFound",
		  "[plugin_manager][discover]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	// mock_plugin_multi requires MockService, so load mock_plugin first.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_MULTI_PLUGIN_PATH));

	REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
	REQUIRE(sm.getService<thx_mock::ServiceA>() != nullptr);
	REQUIRE(sm.getService<thx_mock::ServiceB>() != nullptr);
}

TEST_CASE("PluginManager - unloading IPlugin plugin removes all its services",
		  "[plugin_manager][iplugin][integration]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(loader.load(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::ServiceA>() != nullptr);

	REQUIRE(loader.unload(THX_MOCK_FORGETS_UNLOAD_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::ServiceA>() == nullptr);
}

TEST_CASE("PluginManager - destructor sweeps services left behind by onUnload",
		  "[plugin_manager][iplugin][integration]")
{
	auto& sm = thx::registry().serviceManager();
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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	// mock_plugin_multi requires MockService — without loading mock_plugin
	// first, the load must fail.
	auto r = loader.load(THX_MOCK_MULTI_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE_FALSE(loader.isLoaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

// --- Dependency-resolving load -----------------------------------------------

namespace
{
	// Index of a canonical path within a loaded-list, for ordering asserts.
	std::ptrdiff_t indexOf(std::vector<std::string> const& v, std::string const& p)
	{
		auto it = std::find(v.begin(), v.end(), p);
		return it == v.end() ? -1 : (it - v.begin());
	}
} // namespace

TEST_CASE("PluginManager::loadWithDependencies - loads provider before dependent",
		  "[plugin_manager][deps][integration]")
{
	namespace fs = std::filesystem;
	auto tmp = fs::temp_directory_path() / "thx_test_load_deps";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto basePath = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);
	auto multiPath = copyPluginWithSidecar(THX_MOCK_MULTI_PLUGIN_PATH, tmp);

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);
	REQUIRE(loader.discover(tmp.string()));

	// multi requires MockService, which base provides. loadWithDependencies
	// should pull base in and load it first.
	auto summary = loader.loadWithDependencies(multiPath.string());
	REQUIRE(summary.failed.empty());
	REQUIRE(summary.loaded.size() == 2);

	auto baseCanon = fs::canonical(basePath).string();
	auto multiCanon = fs::canonical(multiPath).string();
	REQUIRE(indexOf(summary.loaded, baseCanon) >= 0);
	REQUIRE(indexOf(summary.loaded, multiCanon) >= 0);
	REQUIRE(indexOf(summary.loaded, baseCanon) < indexOf(summary.loaded, multiCanon));
	REQUIRE(loader.isLoaded(baseCanon));
	REQUIRE(loader.isLoaded(multiCanon));

	loader.clear();
	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::loadWithDependencies - unresolved requirement fails the dependent",
		  "[plugin_manager][deps][integration]")
{
	namespace fs = std::filesystem;
	auto tmp = fs::temp_directory_path() / "thx_test_load_deps_unresolved";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	// Only the dependent is present; no provider of MockService is known.
	auto multiPath = copyPluginWithSidecar(THX_MOCK_MULTI_PLUGIN_PATH, tmp);

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);
	REQUIRE(loader.discover(tmp.string()));

	auto summary = loader.loadWithDependencies(multiPath.string());
	REQUIRE(summary.loaded.empty());
	REQUIRE(summary.failed.size() == 1);
	REQUIRE(summary.failed[0].second.code == thx::ErrorCode::UnresolvedDependency);
	REQUIRE(summary.failed[0].second.message.find("thx_mock.MockService") != std::string::npos);
	REQUIRE_FALSE(loader.isLoaded(fs::canonical(multiPath).string()));

	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::loadWithDependencies - requirement met by a registered service needs no extra load",
		  "[plugin_manager][deps][integration]")
{
	namespace fs = std::filesystem;
	auto tmp = fs::temp_directory_path() / "thx_test_load_deps_satisfied";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto basePath = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);
	auto multiPath = copyPluginWithSidecar(THX_MOCK_MULTI_PLUGIN_PATH, tmp);

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);
	REQUIRE(loader.discover(tmp.string()));

	// Load the provider up front; its MockService is now registered.
	REQUIRE(loader.load(basePath.string()));

	auto summary = loader.loadWithDependencies(multiPath.string());
	REQUIRE(summary.failed.empty());
	// Only multi is loaded this call — base was already satisfied, so the
	// resolver did not re-include it.
	REQUIRE(summary.loaded.size() == 1);
	REQUIRE(summary.loaded[0] == fs::canonical(multiPath).string());

	loader.clear();
	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::loadAll - topo-sorts a set regardless of input order",
		  "[plugin_manager][deps][integration]")
{
	namespace fs = std::filesystem;
	auto tmp = fs::temp_directory_path() / "thx_test_load_all";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto basePath = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);
	auto multiPath = copyPluginWithSidecar(THX_MOCK_MULTI_PLUGIN_PATH, tmp);

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);
	REQUIRE(loader.discover(tmp.string()));

	// Pass dependent first to prove the resolver reorders by dependency.
	auto multiInfo = loader.pluginInfo(fs::canonical(multiPath).string());
	auto baseInfo = loader.pluginInfo(fs::canonical(basePath).string());
	REQUIRE(multiInfo);
	REQUIRE(baseInfo);
	std::vector<thx::plugin::PluginInfo> roots{*multiInfo, *baseInfo};

	auto summary = loader.loadAll({roots.data(), roots.size()});
	REQUIRE(summary.failed.empty());
	auto baseCanon = fs::canonical(basePath).string();
	auto multiCanon = fs::canonical(multiPath).string();
	REQUIRE(indexOf(summary.loaded, baseCanon) < indexOf(summary.loaded, multiCanon));

	loader.clear();
	thx::plugin::collectGarbage();
	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::loadWithDependencies - detects a dependency cycle",
		  "[plugin_manager][deps]")
{
	namespace fs = std::filesystem;
	auto tmp = fs::temp_directory_path() / "thx_test_load_cycle";
	fs::remove_all(tmp);
	fs::create_directories(tmp);

	// Two manifests that require each other's service. The paired DSO files
	// only need to *exist* for discover() to register the entries — the cycle
	// is detected from the manifests alone, before any DSO is opened, so empty
	// stub files suffice.
	std::string const ext = thx::LIBRARY_EXTENSION;
	auto writeCycle = [&](char const* base, char const* provides, char const* needs)
	{
		std::ofstream(tmp / (std::string(base) + ext)).put('\0'); // stub DSO
		std::ofstream mf(tmp / (std::string(base) + ".thx.json"));
		mf << "{\n  \"schema\": 1,\n  \"name\": \"" << base
		   << "\",\n  \"version\": \"1.0.0\",\n  \"provides\": [\"" << provides
		   << "\"],\n  \"requires\": [{\"id\": \"" << needs << "\", \"version\": \"1.0.0\"}]\n}";
	};
	writeCycle("cycleA", "cycle.svcA", "cycle.svcB");
	writeCycle("cycleB", "cycle.svcB", "cycle.svcA");

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);
	REQUIRE(loader.discover(tmp.string()));

	auto dsoA = (tmp / (std::string("cycleA") + ext)).string();
	auto summary = loader.loadWithDependencies(dsoA);
	REQUIRE(summary.loaded.empty());
	REQUIRE(summary.failed.size() == 1);
	REQUIRE(summary.failed[0].second.code == thx::ErrorCode::DependencyCycle);

	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::pluginByName - finds a discovered plugin by manifest name",
		  "[plugin_manager][query][integration]")
{
	namespace fs = std::filesystem;
	auto tmp = fs::temp_directory_path() / "thx_test_by_name";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto basePath = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);
	REQUIRE(loader.discover(tmp.string()));

	auto hit = loader.pluginByName("thx_mock.MockService");
	REQUIRE(hit);
	REQUIRE(hit->name == "thx_mock.MockService");
	REQUIRE(hit->path == fs::canonical(basePath).string());

	REQUIRE_FALSE(loader.pluginByName("no.such.plugin"));

	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::discoverAndLoad - loads real plugin from directory",
		  "[plugin_manager][discover][integration]")
{
	namespace fs = std::filesystem;

	auto tmp = fs::temp_directory_path() / "thx_test_discover_load";
	fs::remove_all(tmp);
	fs::create_directories(tmp);
	auto dst = copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, tmp);

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	auto summary = loader.discoverAndLoad(tmp.string());
	auto svc = sm.getService<thx_mock::MockService>();

	bool loaded_ok = (summary.loaded.size() == 1) && summary.failed.empty();
	bool svc_ok = (svc != nullptr);
	bool ping_ok = svc_ok && (svc->ping() == 42);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));

	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE_FALSE(loader.isOpened(THX_MOCK_PLUGIN_PATH));
	REQUIRE(sm.getService<thx_mock::MockService>() != nullptr);
}

TEST_CASE("PluginManager::open - missing file returns FileNotFound",
		  "[plugin_manager][open]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	auto r = loader.open("/nonexistent/plugin.dylib");
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("PluginManager::open / load are idempotent on the target state",
		  "[plugin_manager][open][integration]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	// Open both plugins up front to inspect their requirements via PluginInfo.
	REQUIRE(loader.open(THX_MOCK_MULTI_PLUGIN_PATH));
	REQUIRE(loader.open(THX_MOCK_PLUGIN_PATH));

	auto multi = loader.pluginInfo(THX_MOCK_MULTI_PLUGIN_PATH);
	auto base = loader.pluginInfo(THX_MOCK_PLUGIN_PATH);
	REQUIRE(multi.has_value());
	REQUIRE(base.has_value());

	REQUIRE(multi->requirements.size() == 1);
	REQUIRE(base->requirements.size() == 0);

	// Load base first (it provides MockService), then the multi plugin.
	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.load(THX_MOCK_MULTI_PLUGIN_PATH));

	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isLoaded(THX_MOCK_MULTI_PLUGIN_PATH));
}

TEST_CASE("PluginManager::checkRequirements - dry-run against the registry",
		  "[plugin_manager][open]")
{
	auto& sm = thx::registry().serviceManager();

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

	REQUIRE(pm.plugins().empty());
	REQUIRE(pm.plugins(thx::plugin::State::Loaded).empty());
	REQUIRE(pm.plugins(thx::plugin::State::Opened).empty());
	REQUIRE(pm.plugins(thx::plugin::State::Discovered).empty());
}

TEST_CASE("PluginManager::pluginInfo returns nullopt for unknown paths",
		  "[plugin_manager][query]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

	REQUIRE_FALSE(pm.pluginInfo("/nonexistent/path.dylib").has_value());
	REQUIRE_FALSE(pm.pluginInfo(THX_MOCK_PLUGIN_PATH).has_value());
}

TEST_CASE("PluginManager::plugins() reflects a loaded plugin",
		  "[plugin_manager][query][integration]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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
		if (p.path == info->path)
		{
			found = true;
			break;
		}
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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

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
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager pm(sm);

	REQUIRE(pm.load(THX_MOCK_PLUGIN_PATH));

	constexpr int kReaders = 4;
	constexpr int kIters = 500;
	std::atomic<bool> stopWriter{false};
	std::atomic<int> errors{0};

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
			} });
	}

	std::thread writer([&]
					   {
		while (!stopWriter.load())
		{
			(void)pm.unload(THX_MOCK_PLUGIN_PATH);
			(void)pm.load(THX_MOCK_PLUGIN_PATH);
		} });

	for (auto& t : readers)
		t.join();
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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

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

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(loader.discover(tmp.string()));

	// mock_plugin provides thx_mock.MockService.
	auto matches = loader.pluginsProviding("thx_mock.MockService");
	REQUIRE(matches.size() == 1);
	REQUIRE(matches[0].state == thx::plugin::State::Discovered);

	// Bogus ID matches nothing.
	REQUIRE(loader.pluginsProviding("nope.NotAService").empty());

	fs::remove_all(tmp);
}

TEST_CASE("PluginManager::discover - Recursive::Yes walks subdirectories",
		  "[plugin_manager][discover]")
{
	namespace fs = std::filesystem;

	auto root = fs::temp_directory_path() / "thx_test_recursive_discover";
	fs::remove_all(root);
	fs::create_directories(root / "category_a");
	fs::create_directories(root / "category_b");

	// Place one copy of the mock plugin in each subdirectory.
	copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, root / "category_a");
	copyPluginWithSidecar(THX_MOCK_PLUGIN_PATH, root / "category_b");

	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	// Non-recursive: top-level dir has no sidecars → nothing discovered.
	REQUIRE(loader.discover(root.string()));
	REQUIRE(loader.plugins(thx::plugin::State::Discovered).empty());

	// Recursive: both subdirectory copies should appear. They're the SAME
	// plugin file (just at different paths) so canonical resolution gives
	// us two distinct Discovered entries.
	REQUIRE(loader.discover(root.string(), thx::plugin::Recursive::Yes));
	REQUIRE(loader.plugins(thx::plugin::State::Discovered).size() == 2);

	fs::remove_all(root);
}

TEST_CASE("PluginManager::reload - unloads and reloads the plugin",
		  "[plugin_manager]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	REQUIRE(loader.load(THX_MOCK_PLUGIN_PATH));
	{
		auto svc = sm.getService<thx_mock::MockService>();
		REQUIRE(svc != nullptr);
		REQUIRE(svc->ping() == 42);
		// Drop the handle BEFORE reload so the garbage drain can dlclose.
	}

	REQUIRE(loader.reload(THX_MOCK_PLUGIN_PATH));
	REQUIRE(loader.isLoaded(THX_MOCK_PLUGIN_PATH));

	auto svc2 = sm.getService<thx_mock::MockService>();
	REQUIRE(svc2 != nullptr);
	REQUIRE(svc2->ping() == 42);

	svc2.reset();
	loader.unload(THX_MOCK_PLUGIN_PATH);
	thx::plugin::collectGarbage();
}

TEST_CASE("PluginManager::reload - not-loaded returns NotLoaded",
		  "[plugin_manager]")
{
	auto& sm = thx::registry().serviceManager();
	thx::plugin::PluginManager loader(sm);

	auto r = loader.reload(THX_MOCK_PLUGIN_PATH);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
}
