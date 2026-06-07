/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/log.h>
#include <thx/service/service.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

namespace
{

	// Capturing log sink. thx::logMessage() forwards every record to the installed
	// LogSink; this one copies each into std::string storage (LogRecord::message
	// is a non-owning StringView valid only during the sink call).
	struct Capture
	{
		struct Record
		{
			thx::LogLevel level;
			thx::rtti::SourceLocation location;
			std::string message;
		};

		std::vector<Record> records;

		bool hasLevel(thx::LogLevel lvl) const
		{
			for (auto const& r : records)
				if (r.level == lvl)
					return true;
			return false;
		}
	};

	void captureSink(thx::LogRecord const& r, void* userdata)
	{
		auto* cap = static_cast<Capture*>(userdata);
		cap->records.push_back({r.level, r.location,
								std::string(r.message.data(), r.message.size())});
	}

	// RAII: installs a Capture as the process-wide sink and restores the built-in
	// stderr fallback on scope exit — crucially *before* the Capture is destroyed,
	// so no later emit can touch freed storage.
	struct SinkGuard
	{
		Capture cap;
		SinkGuard() { thx::setSink(&captureSink, &cap); }
		~SinkGuard() { thx::setSink(nullptr, nullptr); }
	};

	// A throwaway service used to provoke a duplicate-registration diagnostic.
	struct DummyService : thx::service::Service<DummyService>
	{
		static constexpr thx::Version staticVersion() { return thx::Version{1, 0, 0}; }
	};

} // namespace

// ---------------------------------------------------------------------------
// thx::logMessage() forwards to the installed sink
// ---------------------------------------------------------------------------

TEST_CASE("log - record reaches the installed sink", "[log]")
{
	SinkGuard g;
	thx::logMessage(thx::LogLevel::Info, "hello from test");

	REQUIRE(g.cap.records.size() == 1);
	REQUIRE(g.cap.records[0].level == thx::LogLevel::Info);
	REQUIRE(g.cap.records[0].message == "hello from test");
}

TEST_CASE("log - level shortcuts map to the right LogLevel", "[log]")
{
	SinkGuard g;
	thx::logMessage(thx::LogLevel::Debug, "d");
	thx::logMessage(thx::LogLevel::Info, "i");
	thx::logMessage(thx::LogLevel::Warn, "w");
	thx::logMessage(thx::LogLevel::Error, "e");

	REQUIRE(g.cap.records.size() == 4);
	REQUIRE(g.cap.records[0].level == thx::LogLevel::Debug);
	REQUIRE(g.cap.records[1].level == thx::LogLevel::Info);
	REQUIRE(g.cap.records[2].level == thx::LogLevel::Warn);
	REQUIRE(g.cap.records[3].level == thx::LogLevel::Error);
	REQUIRE(g.cap.records[3].message == "e");
}

TEST_CASE("log - captures call-site source location", "[log]")
{
	SinkGuard g;
	int expected_line = __LINE__ + 1;
	thx::logMessage(thx::LogLevel::Debug, "location check");

	REQUIRE(!g.cap.records.empty());
	auto const& loc = g.cap.records[0].location;
	REQUIRE(loc.line == expected_line);
	REQUIRE(std::string(loc.file).find("test_diagnostics") != std::string::npos);
	REQUIRE(std::string(loc.function).size() > 0);
}

TEST_CASE("log - no installed sink falls back to stderr without crashing", "[log]")
{
	thx::setSink(nullptr, nullptr); // ensure the built-in fallback is active
	// Goes to the built-in stderr writer; we can't capture it, only assert the
	// call is safe.
	REQUIRE_NOTHROW(thx::logMessage(thx::LogLevel::Info, "this goes to stderr"));
}

TEST_CASE("log - setSink(nullptr) restores the fallback", "[log]")
{
	{
		SinkGuard g;
		thx::logMessage(thx::LogLevel::Info, "captured");
		REQUIRE(g.cap.records.size() == 1);
	}
	// Guard out of scope: the sink is cleared. A further emit must not reach the
	// (now destroyed) capture, and must be safe.
	REQUIRE_NOTHROW(thx::logMessage(thx::LogLevel::Info, "after guard"));
}

// ---------------------------------------------------------------------------
// assertThat
// ---------------------------------------------------------------------------

TEST_CASE("assertThat - true condition does not log", "[assert]")
{
	SinkGuard g;
	thx::assertThat(true, "should not appear");
	REQUIRE(g.cap.records.empty());
}

#if defined(NDEBUG)
TEST_CASE("assertThat - false condition logs Error in release build", "[assert]")
{
	SinkGuard g;
	thx::assertThat(false, "intentional failure");

	REQUIRE(g.cap.records.size() == 1);
	REQUIRE(g.cap.records[0].level == thx::LogLevel::Error);
	REQUIRE(g.cap.records[0].message == "intentional failure");
}

TEST_CASE("assertThat - captures source location on failure", "[assert]")
{
	SinkGuard g;
	int expected_line = __LINE__ + 1;
	thx::assertThat(false, "location check");

	REQUIRE(!g.cap.records.empty());
	REQUIRE(g.cap.records[0].location.line == expected_line);
}
#endif

// ---------------------------------------------------------------------------
// ServiceManager diagnostics flow through the installed sink — and doing so
// does NOT deadlock. ServiceManager emits its duplicate-registration / null-
// factory diagnostics *after* releasing its registry lock, precisely so a sink
// that resolves a service (the in-tree log bridge looks up an ILogService under
// the ServiceManager read lock) can't re-enter the exclusive lock. The capture
// sink here doesn't touch the ServiceManager, but the ordering contract is what
// these cases pin down.
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - duplicate registration routes a warning to the sink",
		  "[log][service_manager]")
{
	SinkGuard g;

	REQUIRE(thx::service::registerService<DummyService>());
	REQUIRE_FALSE(thx::service::registerService<DummyService>()); // duplicate → warn

	REQUIRE(g.cap.hasLevel(thx::LogLevel::Warn));

	thx::service::unregisterService<DummyService>();
}

TEST_CASE("ServiceManager - null factory routes an error to the sink",
		  "[log][service_manager]")
{
	SinkGuard g;

	// Empty factory: invoke is null → registerService logs an Error.
	REQUIRE_FALSE(thx::service::registerService(
		thx::service::ServiceID("test.NullFactory"), thx::Version{1, 0, 0},
		thx::service::ServiceFactory{}));

	REQUIRE(g.cap.hasLevel(thx::LogLevel::Error));
}
