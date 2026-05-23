/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/plugin/plugin.h>
#include <thx/registry.h>
#include <thx/service/iservice.h>
#include <thx/service/service.h>

#include "mock_plugin.h"

#ifndef THX_MOCK_PLUGIN_PATH
#  error "THX_MOCK_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
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
}

TEST_CASE("thx::service:: facades round-trip register / get / unregister",
          "[facade][service]")
{
	using namespace thx::service;

	REQUIRE(getService<FacadeProbe>() == nullptr);

	REQUIRE(registerService<FacadeProbe>([]
	    { return std::make_shared<FacadeProbe>(); }));

	auto svc = getService<FacadeProbe>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->answer() == 42);
	svc.reset();

	REQUIRE(unregisterService<FacadeProbe>());
	REQUIRE(getService<FacadeProbe>() == nullptr);
}

TEST_CASE("thx::service::listServices reflects the Registry-owned manager",
          "[facade][service]")
{
	// Register a single service via the facade and confirm it appears in
	// the listServices snapshot.
	REQUIRE(thx::service::registerService<FacadeProbe>([]
	    { return std::make_shared<FacadeProbe>(); }));

	bool found = false;
	for (auto const& info : thx::service::listServices())
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

TEST_CASE("thx::plugin:: facades cover the load lifecycle",
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

	// listPlugins includes our entry.
	auto entries = thx::plugin::listPlugins();
	bool found = false;
	for (auto const& e : entries)
		if (e.path == THX_MOCK_PLUGIN_PATH || e.pluginName == "thx_mock.MockService")
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

TEST_CASE("thx::plugin::open + load(OpenedPlugin) work through the facade",
          "[facade][plugin][integration]")
{
	auto opened = thx::plugin::open(THX_MOCK_PLUGIN_PATH);
	REQUIRE(opened);
	REQUIRE(bool(opened.value()));

	auto r = thx::plugin::load(std::move(opened.value()));
	REQUIRE(r);
	REQUIRE(thx::plugin::isLoaded(THX_MOCK_PLUGIN_PATH));

	REQUIRE(thx::plugin::unload(THX_MOCK_PLUGIN_PATH));
	thx::plugin::collectGarbage();
}

TEST_CASE("thx::plugin::checkRequirements operates on the Registry's ServiceManager",
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
