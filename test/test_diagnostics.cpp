/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/log.h>
#include <thx/plugin/plugin_manager.h>
#include <thx/service/service_manager.h>

#include "mock_plugin.h"

#ifndef THX_MOCK_PLUGIN_PATH
#  error "THX_MOCK_PLUGIN_PATH not defined"
#endif

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

namespace
{

struct CapturingSink : thx::ILogSink
{
	std::vector<thx::LogRecord> records;

	void write(thx::LogRecord const& r) override { records.push_back(r); }

	bool hasLevel(thx::LogLevel lvl) const
	{
		for (auto const& r : records)
			if (r.level == lvl) return true;
		return false;
	}

	bool hasMessageContaining(std::string const& substr) const
	{
		for (auto const& r : records)
			if (r.message.find(substr) != std::string::npos) return true;
		return false;
	}
};

// RAII guard: installs a capturing sink and restores the default on destruction.
struct SinkGuard
{
	std::shared_ptr<CapturingSink> sink = std::make_shared<CapturingSink>();

	SinkGuard()  { thx::setLogSink(sink); }
	~SinkGuard() { thx::restoreDefaultLogSink(); }

	std::vector<thx::LogRecord> const& records() const { return sink->records; }

	bool hasLevel(thx::LogLevel lvl) const { return sink->hasLevel(lvl); }
	bool hasMessageContaining(std::string const& s) const { return sink->hasMessageContaining(s); }
};

// Minimal concrete IService for unit tests that don't need a real plugin.
struct MinimalService : thx::IService
{
	thx::ServiceID id()      const override { return thx::ServiceID("test.Minimal"); }
	thx::Version   version() const override { return thx::Version{1, 0, 0};     }
};

auto make_minimal = []() -> std::shared_ptr<thx::IService>
{
	return std::make_shared<MinimalService>();
};

} // namespace

// ---------------------------------------------------------------------------
// ILogSink / thx::log
// ---------------------------------------------------------------------------

TEST_CASE("log - record reaches installed sink", "[log]")
{
	SinkGuard g;
	thx::log(thx::LogLevel::Info, "hello from test");

	REQUIRE(g.records().size() == 1);
	REQUIRE(g.records()[0].level   == thx::LogLevel::Info);
	REQUIRE(g.records()[0].message == "hello from test");
}

TEST_CASE("log - all LogLevel values are routed", "[log]")
{
	SinkGuard g;
	thx::log(thx::LogLevel::Debug, "d");
	thx::log(thx::LogLevel::Info,  "i");
	thx::log(thx::LogLevel::Warn,  "w");
	thx::log(thx::LogLevel::Error, "e");

	REQUIRE(g.records().size() == 4);
	REQUIRE(g.records()[0].level == thx::LogLevel::Debug);
	REQUIRE(g.records()[3].level == thx::LogLevel::Error);
}

TEST_CASE("log - captures call-site source location", "[log]")
{
	SinkGuard g;
	int expected_line = __LINE__ + 1;
	thx::log(thx::LogLevel::Debug, "location check");

	REQUIRE(!g.records().empty());
	auto const& loc = g.records()[0].location;
	REQUIRE(loc.line == expected_line);
	REQUIRE(std::string(loc.file).find("test_diagnostics") != std::string::npos);
	REQUIRE(std::string(loc.function).size() > 0);
}

TEST_CASE("setLogSink - nullptr silences logging", "[log]")
{
	thx::setLogSink(nullptr);
	// No crash, no output anywhere â€” the call simply drops.
	thx::log(thx::LogLevel::Info, "this goes nowhere");
	thx::restoreDefaultLogSink();
}

TEST_CASE("restoreDefaultLogSink - resumes stderr sink without crashing", "[log]")
{
	auto sink = std::make_shared<CapturingSink>();
	thx::setLogSink(sink);
	thx::log(thx::LogLevel::Info, "captured");
	REQUIRE(sink->records.size() == 1);

	thx::restoreDefaultLogSink();
	// Subsequent log goes to stderr (the default), not the captured sink.
	thx::log(thx::LogLevel::Info, "default again");
	REQUIRE(sink->records.size() == 1);
}

TEST_CASE("setLogSink - replacing sink mid-stream", "[log]")
{
	auto sink1 = std::make_shared<CapturingSink>();
	auto sink2 = std::make_shared<CapturingSink>();

	thx::setLogSink(sink1);
	thx::log(thx::LogLevel::Info, "to sink1");

	thx::setLogSink(sink2);
	thx::log(thx::LogLevel::Info, "to sink2");

	thx::restoreDefaultLogSink();

	REQUIRE(sink1->records.size() == 1);
	REQUIRE(sink2->records.size() == 1);
	REQUIRE(sink1->records[0].message == "to sink1");
	REQUIRE(sink2->records[0].message == "to sink2");
}

// ---------------------------------------------------------------------------
// assertThat
// ---------------------------------------------------------------------------

TEST_CASE("assertThat - true condition does not log", "[assert]")
{
	SinkGuard g;
	thx::assertThat(true, "should not appear");
	REQUIRE(g.records().empty());
}

#if defined(NDEBUG)
TEST_CASE("assertThat - false condition logs Error in release build", "[assert]")
{
	SinkGuard g;
	thx::assertThat(false, "intentional failure");

	REQUIRE(g.records().size() == 1);
	REQUIRE(g.records()[0].level   == thx::LogLevel::Error);
	REQUIRE(g.records()[0].message == "intentional failure");
}

TEST_CASE("assertThat - captures source location on failure", "[assert]")
{
	SinkGuard g;
	int expected_line = __LINE__ + 1;
	thx::assertThat(false, "location check");

	REQUIRE(!g.records().empty());
	REQUIRE(g.records()[0].location.line == expected_line);
}
#endif

// ---------------------------------------------------------------------------
// ServiceManager diagnostic output routes through the log sink
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - null factory logs Error", "[log][service_manager]")
{
	SinkGuard g;
	thx::ServiceManager sm;

	sm.registerService(thx::ServiceID("test.Null"), thx::Version{1, 0, 0}, nullptr);

	REQUIRE(g.hasLevel(thx::LogLevel::Error));
}

TEST_CASE("ServiceManager - incompatible major version logs Warn", "[log][service_manager]")
{
	SinkGuard g;
	thx::ServiceManager sm;

	sm.registerService(thx::ServiceID("test.Minimal"), thx::Version{1, 0, 0}, make_minimal);
	// Attempt to register same ID at major version 2 â€” incompatible.
	sm.registerService(thx::ServiceID("test.Minimal"), thx::Version{2, 0, 0}, make_minimal);

	REQUIRE(g.hasLevel(thx::LogLevel::Warn));
}

TEST_CASE("ServiceManager - unregister unknown ID logs Warn", "[log][service_manager]")
{
	SinkGuard g;
	thx::ServiceManager sm;

	sm.unregisterService(thx::ServiceID("test.Unknown"));

	REQUIRE(g.hasLevel(thx::LogLevel::Warn));
}

TEST_CASE("ServiceManager - errors route to installed sink", "[log][service_manager]")
{
	auto sink = std::make_shared<CapturingSink>();
	thx::setLogSink(sink);

	thx::ServiceManager sm;
	sm.registerService(thx::ServiceID("test.Static"), thx::Version{1, 0, 0}, nullptr);

	thx::restoreDefaultLogSink();

	REQUIRE(sink->hasLevel(thx::LogLevel::Error));
}

// ---------------------------------------------------------------------------
// ServiceManager::listServices
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager::listServices - empty initially", "[introspection]")
{
	thx::ServiceManager sm;
	REQUIRE(sm.listServices().empty());
}

TEST_CASE("ServiceManager::listServices - returns registered entry", "[introspection]")
{
	thx::ServiceManager sm;
	sm.registerService(thx::ServiceID("test.Minimal"), thx::Version{1, 0, 0}, make_minimal);

	auto svcs = sm.listServices();
	REQUIRE(svcs.size() == 1);
	REQUIRE(svcs[0].id == thx::ServiceID("test.Minimal"));
}

TEST_CASE("ServiceManager - duplicate registration is rejected",
          "[introspection]")
{
	thx::ServiceManager sm;
	REQUIRE(sm.registerService(thx::ServiceID("test.Minimal"),
	                            thx::Version{1, 0, 0}, make_minimal));
	REQUIRE_FALSE(sm.registerService(thx::ServiceID("test.Minimal"),
	                                  thx::Version{1, 0, 0}, make_minimal));
	REQUIRE(sm.listServices().size() == 1);
}

TEST_CASE("ServiceManager::listServices - entry removed after unregister",
          "[introspection]")
{
	thx::ServiceManager sm;
	sm.registerService(thx::ServiceID("test.Minimal"), thx::Version{1, 0, 0}, make_minimal);
	sm.unregisterService(thx::ServiceID("test.Minimal"));

	REQUIRE(sm.listServices().empty());
}

TEST_CASE("ServiceManager::listServices - multiple independent services",
          "[introspection]")
{
	thx::ServiceManager sm;
	sm.registerService(thx::ServiceID("test.A"), thx::Version{1, 0, 0}, make_minimal);
	sm.registerService(thx::ServiceID("test.B"), thx::Version{1, 0, 0}, make_minimal);

	REQUIRE(sm.listServices().size() == 2);
}

// ---------------------------------------------------------------------------
// PluginManager::listPlugins
// ---------------------------------------------------------------------------

TEST_CASE("PluginManager::listPlugins - empty before load", "[introspection]")
{
	thx::ServiceManager sm;
	thx::PluginManager   loader(sm);

	REQUIRE(loader.listPlugins().empty());
}

TEST_CASE("PluginManager::listPlugins - entry present after load", "[introspection]")
{
	thx::ServiceManager sm;
	thx::PluginManager   loader(sm);
	loader.load(THX_MOCK_PLUGIN_PATH);

	auto plugins = loader.listPlugins();
	REQUIRE(plugins.size() == 1);
	REQUIRE(!plugins[0].pluginName.empty());
	REQUIRE(plugins[0].services.size() == 1);
	REQUIRE(plugins[0].services[0] == thx_mock::MockService::staticId());
}

TEST_CASE("PluginManager::listPlugins - empty after unload", "[introspection]")
{
	thx::ServiceManager sm;
	thx::PluginManager   loader(sm);
	loader.load(THX_MOCK_PLUGIN_PATH);
	loader.unload(THX_MOCK_PLUGIN_PATH);

	REQUIRE(loader.listPlugins().empty());
}
