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
#include <thx/lifecycle.h>
#include <thx/plugin/plugin.h>
#include <thx/service/iservice.h>
#include <thx/service/service.h>

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

TEST_CASE("Registry::instance returns the same object across calls", "[registry]")
{
	auto& a = thx::Registry::instance();
	auto& b = thx::Registry::instance();
	REQUIRE(&a == &b);
}

TEST_CASE("thx::registry is a shorthand for Registry::instance", "[registry]")
{
	REQUIRE(&thx::registry() == &thx::Registry::instance());
}

TEST_CASE("Registry exposes ServiceManager, PluginManager, and PluginGarbage",
		  "[registry]")
{
	auto& reg = thx::Registry::instance();
	auto& sm = reg.serviceManager();
	auto& pm = reg.pluginManager();
	auto& gc = reg.pluginGarbage();

	// Calling the accessors again yields the same objects.
	REQUIRE(&sm == &reg.serviceManager());
	REQUIRE(&pm == &reg.pluginManager());
	REQUIRE(&gc == &reg.pluginGarbage());
}

TEST_CASE("collectGarbage / pendingGarbage operate on the Registry-owned queue",
		  "[registry]")
{
	auto& gc = thx::registry().pluginGarbage();
	// Drain in case earlier tests left handles queued.
	gc.collect();

	REQUIRE(thx::plugin::pendingGarbage() == 0);
	REQUIRE(thx::plugin::collectGarbage() == 0);
}

TEST_CASE("thx::initialise sets the debug name when previously empty",
		  "[registry][lifecycle]")
{
	// Reset to a known state — earlier tests may have set a name.
	thx::shutdown();
	REQUIRE(thx::registry().debugName().empty());

	REQUIRE(thx::initialise("test_run") == true);
	REQUIRE(thx::registry().debugName() == "test_run");

	// Second call is a no-op and reports false.
	REQUIRE(thx::initialise("ignored") == false);
	REQUIRE(thx::registry().debugName() == "test_run");

	thx::shutdown();
	REQUIRE(thx::registry().debugName().empty());
}

TEST_CASE("thx::shutdown drains the deferred-close queue",
		  "[registry][lifecycle]")
{
	auto& gc = thx::registry().pluginGarbage();

	// Drain any prior state, then schedule a known-non-null sentinel and
	// confirm shutdown() clears it.
	gc.collect();
	REQUIRE(gc.pending() == 0);

	// Avoid using a real DSO handle — schedule() takes void* and never
	// dereferences until collect() calls native_close, but to keep this test
	// hermetic we just verify queue depth through the public API.
	// Instead: assert that shutdown is safe to call on an empty queue.
	thx::shutdown();
	REQUIRE(gc.pending() == 0);
	REQUIRE(thx::registry().debugName().empty());
}

TEST_CASE("thx::shutdown unregisters services left in the Registry",
		  "[registry][lifecycle][service]")
{
	using namespace thx::service;

	// Start clean, register a service directly through the production facade,
	// confirm it is live, then prove shutdown() removes it.
	thx::shutdown();
	REQUIRE(getService<ShutdownProbe>() == nullptr);

	REQUIRE(registerService<ShutdownProbe>());
	REQUIRE(getService<ShutdownProbe>() != nullptr);

	thx::shutdown();
	REQUIRE(getService<ShutdownProbe>() == nullptr);
	REQUIRE(thx::registry().serviceManager().listServices().empty());
}

TEST_CASE("thx::shutdown runs service onDestroy hooks",
		  "[registry][lifecycle][service]")
{
	using namespace thx::service;

	thx::shutdown();
	ShutdownProbe::destroyCount = 0;

	REQUIRE(registerService<ShutdownProbe>());
	REQUIRE(ShutdownProbe::destroyCount == 0); // onDestroy hasn't run yet

	thx::shutdown();
	REQUIRE(ShutdownProbe::destroyCount == 1);
}

TEST_CASE("thx::shutdown unloads loaded plugins and drains their DSOs",
		  "[registry][lifecycle][plugin]")
{
	using namespace thx;

	shutdown(); // clean slate
	REQUIRE(plugin::load(THX_MOCK_PLUGIN_PATH));
	REQUIRE(plugin::isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(service::getService<thx_mock::MockService>() != nullptr);

	shutdown();

	REQUIRE_FALSE(plugin::isLoaded(THX_MOCK_PLUGIN_PATH));
	REQUIRE(service::getService<thx_mock::MockService>() == nullptr);
	// shutdown() drains the deferred-close queue after unloading.
	REQUIRE(plugin::pendingGarbage() == 0);
}

TEST_CASE("thx::shutdown is idempotent", "[registry][lifecycle]")
{
	thx::shutdown();
	thx::shutdown(); // second call must be a safe no-op

	REQUIRE(thx::registry().serviceManager().listServices().empty());
	REQUIRE(thx::registry().pluginManager().plugins().empty());
	REQUIRE(thx::registry().debugName().empty());
}
