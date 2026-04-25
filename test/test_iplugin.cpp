/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/iplugin.h>
#include <thx/service.h>
#include <thx/service_manager.h>

#include <memory>

namespace
{

	struct ShimTestService : thx::Service<ShimTestService>
	{
		static constexpr thx::Version static_version() { return thx::make_version(1, 0, 0); }

		bool m_constructed{false};
		bool onConstruct() override
		{
			m_constructed = true;
			return true;
		}
	};

	// A custom IPlugin that registers no services, just exercises the interface
	// shape and required() default.
	struct EmptyPlugin : thx::IPlugin
	{
		thx::StringView name()    const override { return "thx.test.EmptyPlugin"; }
		thx::Version    version() const override { return thx::make_version(2, 3, 4); }

		bool onLoad(thx::ServiceManager&)  override { return true; }
		void onUnload(thx::ServiceManager&) override {}
	};

	// A custom IPlugin that registers two services in onLoad and unregisters them
	// in onUnload. Demonstrates the multi-service capability.
	struct ServiceA : thx::Service<ServiceA>
	{
		static constexpr thx::Version static_version() { return thx::make_version(1, 0, 0); }
	};
	struct ServiceB : thx::Service<ServiceB>
	{
		static constexpr thx::Version static_version() { return thx::make_version(1, 0, 0); }
	};

	struct MultiServicePlugin : thx::IPlugin
	{
		thx::StringView name()    const override { return "thx.test.MultiServicePlugin"; }
		thx::Version    version() const override { return thx::make_version(1, 0, 0); }

		bool onLoad(thx::ServiceManager& sm) override
		{
			bool ok_a = sm.register_service<ServiceA>([] { return std::make_shared<ServiceA>(); });
			bool ok_b = sm.register_service<ServiceB>([] { return std::make_shared<ServiceB>(); });
			return ok_a && ok_b;
		}

		void onUnload(thx::ServiceManager& sm) override
		{
			sm.unregister_service<ServiceB>();
			sm.unregister_service<ServiceA>();
		}
	};

} // namespace

TEST_CASE("ServicePluginShim - registers and unregisters one service",
		  "[iplugin]")
{
	thx::ServiceManager sm;
	thx::ServicePluginShim<ShimTestService> shim;

	REQUIRE(sm.get_service<ShimTestService>() == nullptr);
	REQUIRE(shim.onLoad(sm));

	auto svc = sm.get_service<ShimTestService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->m_constructed);

	shim.onUnload(sm);
	REQUIRE(sm.get_service<ShimTestService>() == nullptr);
}

TEST_CASE("ServicePluginShim - reports name and version from the service type",
		  "[iplugin]")
{
	thx::ServicePluginShim<ShimTestService> shim;

	// Compare via the underlying string content; the shim returns the name
	// derived from the C++ qualified type name.
	REQUIRE(thx::StringView(ShimTestService::static_id().name()) == shim.name());
	REQUIRE(shim.version() == thx::make_version(1, 0, 0));
}

TEST_CASE("IPlugin - default required() is empty",
		  "[iplugin]")
{
	EmptyPlugin p;
	REQUIRE(p.required().size() == 0);
	REQUIRE(p.required().empty());
}

TEST_CASE("IPlugin - custom plugin can register multiple services",
		  "[iplugin]")
{
	thx::ServiceManager sm;
	MultiServicePlugin plugin;

	REQUIRE(plugin.onLoad(sm));
	REQUIRE(sm.get_service<ServiceA>() != nullptr);
	REQUIRE(sm.get_service<ServiceB>() != nullptr);

	plugin.onUnload(sm);
	REQUIRE(sm.get_service<ServiceA>() == nullptr);
	REQUIRE(sm.get_service<ServiceB>() == nullptr);
}

TEST_CASE("IPlugin - onLoad returning false does not register anything",
		  "[iplugin]")
{
	struct BailingPlugin : thx::IPlugin
	{
		thx::StringView name()    const override { return "thx.test.Bailing"; }
		thx::Version    version() const override { return thx::make_version(1, 0, 0); }

		bool onLoad(thx::ServiceManager&)   override { return false; }
		void onUnload(thx::ServiceManager&) override {}
	};

	thx::ServiceManager sm;
	BailingPlugin p;

	REQUIRE_FALSE(p.onLoad(sm));
	REQUIRE(sm.list_services().empty());
}
