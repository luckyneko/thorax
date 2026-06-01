/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/log/log.h>
#include <thx/log/log_service.h>
#include <thx/service/service.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Test helpers
// ---------------------------------------------------------------------------

namespace
{

	// Capturing logging service. Registered like any other service; thx::log::*
	// forwards to it. LogRecord::message is a non-owning StringView valid only
	// during write(), so we copy it into our own std::string storage.
	struct CapturingService : thx::log::ILogService
	{
		struct Record
		{
			thx::log::LogLevel level;
			thx::log::SourceLocation location;
			std::string message;
		};

		std::vector<Record> records;

		void write(thx::log::LogRecord const& r) override
		{
			records.push_back({r.level, r.location,
							   std::string(r.message.data(), r.message.size())});
		}

		bool hasLevel(thx::log::LogLevel lvl) const
		{
			for (auto const& r : records)
				if (r.level == lvl)
					return true;
			return false;
		}
	};

	// A throwaway service used to provoke a duplicate-registration diagnostic.
	struct DummyService : thx::service::Service<DummyService>
	{
		static constexpr thx::Version staticVersion() { return thx::Version{1, 0, 0}; }
	};

	// Registers a CapturingService as the process-wide ILogService and returns a
	// raw pointer to it. The pointer stays valid until the service is
	// unregistered (the per-test reset listener calls thx::shutdown()). Only one
	// ILogService may be registered at a time (single-owner registry).
	CapturingService* installCapture()
	{
		CapturingService* cap = nullptr;
		bool ok = thx::service::registerService<CapturingService>(
			[&]() -> thx::service::IService*
			{
				cap = new CapturingService();
				return cap;
			});
		REQUIRE(ok);
		REQUIRE(cap != nullptr);
		return cap;
	}

} // namespace

// ---------------------------------------------------------------------------
// thx::log::write forwards to a registered ILogService
// ---------------------------------------------------------------------------

TEST_CASE("log - record reaches the registered service", "[log]")
{
	auto* cap = installCapture();
	thx::log::write(thx::log::LogLevel::Info, "hello from test");

	REQUIRE(cap->records.size() == 1);
	REQUIRE(cap->records[0].level == thx::log::LogLevel::Info);
	REQUIRE(cap->records[0].message == "hello from test");
}

TEST_CASE("log - level shortcuts map to the right LogLevel", "[log]")
{
	auto* cap = installCapture();
	thx::log::debug("d");
	thx::log::info("i");
	thx::log::warn("w");
	thx::log::error("e");

	REQUIRE(cap->records.size() == 4);
	REQUIRE(cap->records[0].level == thx::log::LogLevel::Debug);
	REQUIRE(cap->records[1].level == thx::log::LogLevel::Info);
	REQUIRE(cap->records[2].level == thx::log::LogLevel::Warn);
	REQUIRE(cap->records[3].level == thx::log::LogLevel::Error);
	REQUIRE(cap->records[3].message == "e");
}

TEST_CASE("log - captures call-site source location", "[log]")
{
	auto* cap = installCapture();
	int expected_line = __LINE__ + 1;
	thx::log::debug("location check");

	REQUIRE(!cap->records.empty());
	auto const& loc = cap->records[0].location;
	REQUIRE(loc.line == expected_line);
	REQUIRE(std::string(loc.file).find("test_diagnostics") != std::string::npos);
	REQUIRE(std::string(loc.function).size() > 0);
}

TEST_CASE("log - no registered service falls back to stderr without crashing", "[log]")
{
	REQUIRE(thx::service::getService<thx::log::ILogService>() == nullptr);
	// Goes to the built-in stderr writer; we can't capture it, only assert the
	// call is safe.
	REQUIRE_NOTHROW(thx::log::info("this goes to stderr"));
}

// ---------------------------------------------------------------------------
// assertThat
// ---------------------------------------------------------------------------

TEST_CASE("assertThat - true condition does not log", "[assert]")
{
	auto* cap = installCapture();
	thx::log::assertThat(true, "should not appear");
	REQUIRE(cap->records.empty());
}

#if defined(NDEBUG)
TEST_CASE("assertThat - false condition logs Error in release build", "[assert]")
{
	auto* cap = installCapture();
	thx::log::assertThat(false, "intentional failure");

	REQUIRE(cap->records.size() == 1);
	REQUIRE(cap->records[0].level == thx::log::LogLevel::Error);
	REQUIRE(cap->records[0].message == "intentional failure");
}

TEST_CASE("assertThat - captures source location on failure", "[assert]")
{
	auto* cap = installCapture();
	int expected_line = __LINE__ + 1;
	thx::log::assertThat(false, "location check");

	REQUIRE(!cap->records.empty());
	REQUIRE(cap->records[0].location.line == expected_line);
}
#endif

// ---------------------------------------------------------------------------
// ServiceManager diagnostics route through the registered service — and doing
// so does NOT deadlock, even though ServiceManager logs its own diagnostics.
// The duplicate-registration warn is emitted after the registry lock is
// released precisely so the forwarding getService<ILogService>() can take its
// shared lock without re-entering the exclusive lock.
// ---------------------------------------------------------------------------

TEST_CASE("ServiceManager - duplicate registration routes a warning to the service",
		  "[log][service_manager]")
{
	auto* cap = installCapture();

	REQUIRE(thx::service::registerService<DummyService>());
	REQUIRE_FALSE(thx::service::registerService<DummyService>()); // duplicate → warn

	REQUIRE(cap->hasLevel(thx::log::LogLevel::Warn));
}

TEST_CASE("ServiceManager - null factory routes an error to the service",
		  "[log][service_manager]")
{
	auto* cap = installCapture();

	// Empty factory: invoke is null → registerService logs an Error.
	REQUIRE_FALSE(thx::service::registerService(
		thx::service::ServiceID("test.NullFactory"), thx::Version{1, 0, 0},
		thx::service::ServiceFactory{}));

	REQUIRE(cap->hasLevel(thx::log::LogLevel::Error));
}
