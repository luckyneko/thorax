/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/log.h>
#include <service/service_manager.h>

#include <memory>
#include <string>
#include <vector>

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
	// No crash, no output anywhere; the call simply drops.
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
// ServiceManager diagnostics route through the installed log sink.
// (Rejection *semantics* are covered in test_service_manager.cpp; this proves
// the diagnostics actually reach the sink.)
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - errors route to installed sink", "[log][service_manager]")
{
	auto sink = std::make_shared<CapturingSink>();
	thx::setLogSink(sink);

	thx::service::ServiceManager sm;
	sm.registerService(thx::service::ServiceID("test.Static"), thx::Version{1, 0, 0},
	                   thx::service::ServiceFactory{});  // empty factory — invoke is null

	thx::restoreDefaultLogSink();

	REQUIRE(sink->hasLevel(thx::LogLevel::Error));
}
