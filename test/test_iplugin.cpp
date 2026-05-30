/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/plugin/iplugin.h>
#include <thx/service/iservice.h>
#include <thx/service/service.h>

#include <memory>

namespace
{

	struct ShimTestService : thx::service::Service<ShimTestService>
	{
		static constexpr thx::Version staticVersion() { return thx::Version{1, 0, 0}; }

		bool m_constructed{false};
		bool onConstruct() override
		{
			m_constructed = true;
			return true;
		}
	};

	// A custom IPlugin that registers no services, just exercises the interface
	// shape and required() default.
	struct EmptyPlugin : thx::plugin::IPlugin
	{
		thx::StringView name()    const override { return "thx.test.EmptyPlugin"; }
		thx::Version    version() const override { return thx::Version{2, 3, 4}; }

		bool onLoad()   override { return true; }
		void onUnload() override {}
	};

	// A custom IPlugin that registers two services in onLoad and unregisters them
	// in onUnload. Demonstrates the multi-service capability.
	struct ServiceA : thx::service::Service<ServiceA>
	{
		static constexpr thx::Version staticVersion() { return thx::Version{1, 0, 0}; }
	};
	struct ServiceB : thx::service::Service<ServiceB>
	{
		static constexpr thx::Version staticVersion() { return thx::Version{1, 0, 0}; }
	};

	struct MultiServicePlugin : thx::plugin::IPlugin
	{
		thx::StringView name()    const override { return "thx.test.MultiServicePlugin"; }
		thx::Version    version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad() override
		{
			bool ok_a = thx::service::registerService<ServiceA>();
			bool ok_b = thx::service::registerService<ServiceB>();
			return ok_a && ok_b;
		}

		void onUnload() override
		{
			thx::service::unregisterService<ServiceB>();
			thx::service::unregisterService<ServiceA>();
		}
	};

} // namespace

TEST_CASE("ServicePluginShim - registers and unregisters one service",
		  "[iplugin]")
{
	using namespace thx::service;
	thx::plugin::ServicePluginShim<ShimTestService> shim;

	REQUIRE(getService<ShimTestService>() == nullptr);
	REQUIRE(shim.onLoad());

	auto svc = getService<ShimTestService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->m_constructed);
	svc.reset();

	shim.onUnload();
	REQUIRE(getService<ShimTestService>() == nullptr);
}

TEST_CASE("ServicePluginShim - reports name and version from the service type",
		  "[iplugin]")
{
	thx::plugin::ServicePluginShim<ShimTestService> shim;

	// Compare via the underlying string content; the shim returns the name
	// derived from the C++ qualified type name.
	REQUIRE(thx::StringView(ShimTestService::staticId().name()) == shim.name());
	REQUIRE(shim.version() == thx::Version{1, 0, 0});
}

TEST_CASE("IPlugin - default required() is empty",
		  "[iplugin]")
{
	EmptyPlugin p;
	REQUIRE(p.required().size() == 0);
	REQUIRE(p.required().empty());
}

TEST_CASE("IPlugin - default provides() is empty",
		  "[iplugin]")
{
	EmptyPlugin p;
	REQUIRE(p.provides().size() == 0);
	REQUIRE(p.provides().empty());
}

TEST_CASE("ServicePluginShim - provides() reports T::staticId()",
		  "[iplugin]")
{
	thx::plugin::ServicePluginShim<ShimTestService> shim;
	auto p = shim.provides();
	REQUIRE(p.size() == 1);
	REQUIRE(p[0] == ShimTestService::staticId());
}

TEST_CASE("IPlugin - custom plugin can register multiple services",
		  "[iplugin]")
{
	using namespace thx::service;
	MultiServicePlugin plugin;

	REQUIRE(plugin.onLoad());
	REQUIRE(getService<ServiceA>() != nullptr);
	REQUIRE(getService<ServiceB>() != nullptr);

	plugin.onUnload();
	REQUIRE(getService<ServiceA>() == nullptr);
	REQUIRE(getService<ServiceB>() == nullptr);
}

TEST_CASE("IPlugin - onLoad returning false does not register anything",
		  "[iplugin]")
{
	struct BailingPlugin : thx::plugin::IPlugin
	{
		thx::StringView name()    const override { return "thx.test.Bailing"; }
		thx::Version    version() const override { return thx::Version{1, 0, 0}; }

		bool onLoad()   override { return false; }
		void onUnload() override {}
	};

	BailingPlugin p;

	REQUIRE_FALSE(p.onLoad());
	REQUIRE(thx::service::listServices().empty());
}
