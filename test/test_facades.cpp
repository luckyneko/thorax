/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <registry.h>
#include <thx/plugin/plugin.h>
#include <thx/service/iservice.h>
#include <thx/service/service.h>

#include "mock_plugin.h"

#include <filesystem>

#ifndef THX_MOCK_PLUGIN_PATH
#	error "THX_MOCK_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// The facade tests exercise the Registry-owned managers. Each test cleans up
// after itself so they can be run in any order alongside other tests that
// share the singleton.

namespace
{
	// Local service that's only used by these tests; constructed via the
	// type-deducing facade.
	struct FacadeProbe : thx::service::Service<FacadeProbe>
	{
		static constexpr thx::Version staticVersion() { return {1, 0, 0}; }
		int answer() const { return 42; }
	};
} // namespace

TEST_CASE("thx::service facades - round-trip register / get / unregister",
		  "[facade][service]")
{
	using namespace thx::service;

	REQUIRE(getService<FacadeProbe>() == nullptr);

	REQUIRE(registerService<FacadeProbe>());

	auto svc = getService<FacadeProbe>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->answer() == 42);
	svc.reset();

	REQUIRE(unregisterService<FacadeProbe>());
	REQUIRE(getService<FacadeProbe>() == nullptr);
}

TEST_CASE("thx::service::listServices - reflects the Registry-owned manager",
		  "[facade][service]")
{
	// Register a single service via the facade and confirm it appears in
	// the listServices snapshot.
	REQUIRE(thx::service::registerService<FacadeProbe>());

	bool found = false;
	for (const auto& info : thx::service::listServices())
	{
		if (info.id == FacadeProbe::staticId())
		{
			found = true;
			REQUIRE(info.version == FacadeProbe::staticVersion());
			break;
		}
	}
	REQUIRE(found);

	REQUIRE(thx::service::unregisterService<FacadeProbe>());
}

TEST_CASE("thx::plugin facades - cover the load lifecycle",
		  "[facade][plugin][integration]")
{
	// Sanity: nothing loaded.
	REQUIRE_FALSE(thx::plugin::isLoaded(THX_MOCK_PLUGIN_PATH));

	// Load via the free-function facade.
	auto loaded = thx::plugin::load(THX_MOCK_PLUGIN_PATH);
	REQUIRE(loaded);
	REQUIRE(thx::plugin::isLoaded(THX_MOCK_PLUGIN_PATH));

	// Service is reachable via the service facade.
	auto svc = thx::service::getService<thx_mock::MockService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->ping() == 42);
	svc.reset();

	// plugins(Loaded) includes our entry.
	auto entries = thx::plugin::plugins(thx::plugin::State::Loaded);
	bool found = false;
	for (const auto& e : entries)
		if (e.path == THX_MOCK_PLUGIN_PATH || e.name == "thx_mock.MockService")
		{
			found = true;
			break;
		}
	REQUIRE(found);

	// Unload via the facade and confirm the service is gone.
	auto unloaded = thx::plugin::unload(THX_MOCK_PLUGIN_PATH);
	REQUIRE(unloaded);
	REQUIRE_FALSE(thx::plugin::isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::service::getService<thx_mock::MockService>() == nullptr);

	// Drain so the DSO unmap doesn't bleed into subsequent tests.
	thx::plugin::collectGarbage();
}

TEST_CASE("thx::plugin::open / load - work through the facade",
		  "[facade][plugin][integration]")
{
	REQUIRE(thx::plugin::open(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::plugin::isOpened(THX_MOCK_PLUGIN_PATH));

	REQUIRE(thx::plugin::load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::plugin::isLoaded(THX_MOCK_PLUGIN_PATH));

	REQUIRE(thx::plugin::unload(THX_MOCK_PLUGIN_PATH));
	thx::plugin::collectGarbage();
}

TEST_CASE("thx::plugin::checkRequirements - operates on the Registry's ServiceManager",
		  "[facade][plugin]")
{
	thx::plugin::ServiceRequirement reqs[] = {
		{thx_mock::MockService::staticId(), thx::Version{1, 0, 0}},
	};

	// Without the mock plugin loaded the requirement is missing.
	auto miss = thx::plugin::checkRequirements({reqs, 1});
	REQUIRE_FALSE(miss);
	REQUIRE(miss.error().code == thx::ErrorCode::NotLoaded);

	// Load the plugin (which provides MockService) and the requirement
	// is satisfied.
	REQUIRE(thx::plugin::load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(thx::plugin::checkRequirements({reqs, 1}));

	// Cleanup.
	REQUIRE(thx::plugin::unload(THX_MOCK_PLUGIN_PATH));
	thx::plugin::collectGarbage();
}

TEST_CASE("thx::plugin facades - name / provides queries and loadWithDependencies",
		  "[facade][plugin][integration]")
{
	namespace fs = std::filesystem;
	auto dir = fs::path(THX_MOCK_PLUGIN_PATH).parent_path().string();
	REQUIRE(thx::plugin::discover(dir));

	auto canonical = fs::canonical(THX_MOCK_PLUGIN_PATH).string();

	// Type-deduced provides filter forwards to the string overload.
	auto providers = thx::plugin::pluginsProviding<thx_mock::MockService>();
	bool foundProvider = false;
	for (const auto& p : providers)
		if (p.path == canonical)
			foundProvider = true;
	REQUIRE(foundProvider);

	// By-name lookup against the Registry-owned manager.
	auto byName = thx::plugin::pluginByName("thx_mock.MockService");
	REQUIRE(byName);
	REQUIRE(byName->path == canonical);
	REQUIRE_FALSE(thx::plugin::pluginByName("no.such.plugin"));

	// loadWithDependencies on a plugin with no unmet requirement just loads it.
	auto summary = thx::plugin::loadWithDependencies(THX_MOCK_PLUGIN_PATH);
	REQUIRE(summary.failed.empty());
	REQUIRE(summary.loaded.size() == 1);
	REQUIRE(thx::service::getService<thx_mock::MockService>() != nullptr);

	// Cleanup.
	REQUIRE(thx::plugin::unload(THX_MOCK_PLUGIN_PATH));
	thx::plugin::collectGarbage();
}
