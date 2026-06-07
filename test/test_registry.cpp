/*
 *  Created by LuckyNeko on 22/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <plugin/plugin_garbage.h>
#include <plugin/plugin_manager.h>
#include <registry.h>
#include <service/service_manager.h>
#include <thx/plugin/plugin.h>
#include <thx/service/iservice.h>
#include <thx/service/service.h>
#include <thx/thorax.h>

#include "mock_plugin.h"

#ifndef THX_MOCK_PLUGIN_PATH
#	error "THX_MOCK_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// The Registry is a process-wide singleton; tests that mutate its state
// (debug name, garbage queue) share it. Each test that pokes at lifecycle
// state resets it on the way out.

namespace
{
	// Probe service registered through the public facade to verify that
	// shutdown() tears registered state down and runs onDestroy.
	struct ShutdownProbe : thx::service::Service<ShutdownProbe>
	{
		static constexpr thx::Version staticVersion() { return {1, 0, 0}; }

		static inline int destroyCount = 0;
		void onDestroy() override { ++destroyCount; }
	};
} // namespace

TEST_CASE("Registry exposes ServiceManager, PluginManager, and PluginGarbage",
		  "[registry]")
{
	auto* reg = thx::registry();
	auto& sm = reg->serviceManager();
	auto& pm = reg->pluginManager();
	auto& gc = reg->pluginGarbage();

	// Calling the accessors again yields the same objects.
	REQUIRE(&sm == &reg->serviceManager());
	REQUIRE(&pm == &reg->pluginManager());
	REQUIRE(&gc == &reg->pluginGarbage());
}

TEST_CASE("collectGarbage / pendingGarbage operate on the Registry-owned queue",
		  "[registry]")
{
	auto& gc = thx::registry()->pluginGarbage();
	// Drain in case earlier tests left handles queued.
	gc.collect();

	REQUIRE(thx::plugin::pendingGarbage() == 0);
	REQUIRE(thx::plugin::collectGarbage() == 0);
}

TEST_CASE("thx::initialise builds the singleton and records the debug name",
		  "[registry][lifecycle]")
{
	// The reset listener already stood the singleton up with a default name;
	// tear it down to a clean slate. shutdown() destroys the Registry outright.
	thx::shutdown();
	REQUIRE(thx::registry() == nullptr);

	thx::Settings settingsA;
	settingsA.name = "test_run";
	REQUIRE(thx::initialise(settingsA) == true);
	REQUIRE(thx::registry() != nullptr);
	REQUIRE(thx::registry()->name() == "test_run");

	// Second call is a no-op while the singleton is up, and reports false.
	thx::Settings settingsB;
	settingsB.name = "ignored";
	REQUIRE(thx::initialise(settingsB) == false);
	REQUIRE(thx::registry()->name() == "test_run");

	thx::shutdown();
	REQUIRE(thx::registry() == nullptr);
}

TEST_CASE("thx::shutdown drains the deferred-close queue",
		  "[registry][lifecycle]")
{
	thx::registry()->pluginGarbage().collect();
	REQUIRE(thx::plugin::pendingGarbage() == 0);

	// shutdown() is safe on an empty queue and tears the singleton down.
	thx::shutdown();
	REQUIRE(thx::registry() == nullptr);
}

TEST_CASE("thx::shutdown unregisters services left in the Registry",
		  "[registry][lifecycle][service]")
{
	using namespace thx::service;

	// The listener handed us a clean, initialised Registry. Register a service
	// directly through the production facade, confirm it is live, then prove
	// shutdown() removes it as it destroys the Registry.
	REQUIRE(getService<ShutdownProbe>() == nullptr);
	REQUIRE(registerService<ShutdownProbe>());
	REQUIRE(getService<ShutdownProbe>() != nullptr);

	thx::shutdown();
	REQUIRE(thx::registry() == nullptr);

	// A fresh Registry carries no trace of the previous one's services.
	thx::initialise();
	REQUIRE(getService<ShutdownProbe>() == nullptr);
	REQUIRE(thx::registry()->serviceManager().listServices().empty());
}

TEST_CASE("thx::shutdown runs service onDestroy hooks",
		  "[registry][lifecycle][service]")
{
	using namespace thx::service;

	ShutdownProbe::destroyCount = 0;

	REQUIRE(registerService<ShutdownProbe>());
	REQUIRE(ShutdownProbe::destroyCount == 0); // onDestroy hasn't run yet

	thx::shutdown();
	REQUIRE(ShutdownProbe::destroyCount == 1);
	REQUIRE(thx::registry() == nullptr);
}

TEST_CASE("thx::shutdown unloads loaded plugins and drains their DSOs",
		  "[registry][lifecycle][plugin]")
{
	using namespace thx;

	REQUIRE(plugin::load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(plugin::isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(service::getService<thx_mock::MockService>() != nullptr);

	// shutdown() runs onUnload, drains the deferred-close queue (dlclosing the
	// DSO), and destroys the Registry — leaving nothing behind.
	shutdown();
	REQUIRE(registry() == nullptr);

	// A fresh Registry starts with nothing loaded and an empty garbage queue.
	initialise();
	REQUIRE_FALSE(plugin::isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(service::getService<thx_mock::MockService>() == nullptr);
	REQUIRE(plugin::pendingGarbage() == 0);
}

TEST_CASE("thx::shutdown is idempotent", "[registry][lifecycle]")
{
	thx::shutdown();
	thx::shutdown(); // second call must be a safe no-op

	REQUIRE(thx::registry() == nullptr);
}
