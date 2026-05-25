/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <plugin/plugin_manager.h>
#include <thx/service/service_manager.h>
#include <thx/plugins/logging/logging_service.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifndef THX_LOGGING_PLUGIN_PATH
#  error "THX_LOGGING_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

using namespace thx::plugins::logging;

// ---------------------------------------------------------------------------
// In-process backend that captures log calls for inspection.
// ---------------------------------------------------------------------------

namespace
{

struct CaptureBackend : ILogBackend
{
	struct Entry
	{
		LogLevel    level;
		std::string message;
	};

	std::vector<Entry> entries;

	void write(LogLevel level, const char* message) override
	{
		entries.push_back({level, message ? message : ""});
	}
};

// Load the logging plugin into a local ServiceManager.
// Returns the loader (RAII — unloads on destruction) or reports failure.
struct Fixture
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader{sm};

	explicit Fixture()
	{
		REQUIRE(loader.load(THX_LOGGING_PLUGIN_PATH));
	}

	std::shared_ptr<ILoggingService> service()
	{
		return sm.getService<ILoggingService>();
	}
};

} // namespace

// ---------------------------------------------------------------------------
// Basic load & service lookup
// ---------------------------------------------------------------------------

TEST_CASE("LoggingPlugin - loads and registers ILoggingService", "[logging_plugin]")
{
	Fixture f;
	REQUIRE(f.service() != nullptr);
}

// ---------------------------------------------------------------------------
// addBackend / log / removeBackend
// ---------------------------------------------------------------------------

TEST_CASE("LoggingPlugin - addBackend routes log() to backend", "[logging_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	auto cap = std::make_shared<CaptureBackend>();
	svc->addBackend(cap);

	svc->log(LogLevel::Info,  "hello");
	svc->log(LogLevel::Error, "world");

	REQUIRE(cap->entries.size() == 2);
	REQUIRE(cap->entries[0].level   == LogLevel::Info);
	REQUIRE(cap->entries[0].message == "hello");
	REQUIRE(cap->entries[1].level   == LogLevel::Error);
	REQUIRE(cap->entries[1].message == "world");
}

TEST_CASE("LoggingPlugin - removeBackend stops delivery", "[logging_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	auto cap = std::make_shared<CaptureBackend>();
	svc->addBackend(cap);
	svc->log(LogLevel::Info, "before");

	svc->removeBackend(cap.get());
	svc->log(LogLevel::Info, "after");

	REQUIRE(cap->entries.size() == 1);
	REQUIRE(cap->entries[0].message == "before");
}

TEST_CASE("LoggingPlugin - expired backend is culled automatically", "[logging_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	{
		auto cap = std::make_shared<CaptureBackend>();
		svc->addBackend(cap);
		svc->log(LogLevel::Debug, "alive");
		REQUIRE(cap->entries.size() == 1);
	} // cap shared_ptr released — weak_ptr in service becomes expired

	// A subsequent log() should evict the expired weak_ptr without crashing.
	REQUIRE_NOTHROW(svc->log(LogLevel::Debug, "after expiry"));
}

TEST_CASE("LoggingPlugin - null backend is ignored", "[logging_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	REQUIRE_NOTHROW(svc->addBackend(nullptr));
	REQUIRE_NOTHROW(svc->log(LogLevel::Info, "should not crash"));
}

// ---------------------------------------------------------------------------
// makeConsoleBackend
// ---------------------------------------------------------------------------

TEST_CASE("LoggingPlugin - makeConsoleBackend returns non-null", "[logging_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	auto con = svc->makeConsoleBackend();
	REQUIRE(con != nullptr);
}

// ---------------------------------------------------------------------------
// makeFileBackend
// ---------------------------------------------------------------------------

TEST_CASE("LoggingPlugin - makeFileBackend writes messages to file", "[logging_plugin]")
{
	namespace fs = std::filesystem;

	auto tmp = (fs::temp_directory_path() / "thx_log_test.txt").string();
	fs::remove(tmp);

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	auto fb = svc->makeFileBackend(tmp.c_str());
	REQUIRE(fb != nullptr);

	svc->addBackend(fb);
	svc->log(LogLevel::Info, "file-message");
	fb.reset(); // release before closing to exercise weak_ptr eviction path

	std::string contents;
	{
		std::ifstream in(tmp);
		REQUIRE(in.is_open());
		contents.assign(std::istreambuf_iterator<char>(in),
		                std::istreambuf_iterator<char>());
	} // close in before remove — Windows requires no open handles to delete

	fs::remove(tmp);

	REQUIRE(contents.find("file-message") != std::string::npos);
}

TEST_CASE("LoggingPlugin - makeFileBackend with null path returns null",
          "[logging_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	REQUIRE(svc->makeFileBackend(nullptr) == nullptr);
}

// ---------------------------------------------------------------------------
// makeRotatingFileBackend
// ---------------------------------------------------------------------------

TEST_CASE("LoggingPlugin - rotating backend rotates when size is exceeded",
          "[logging_plugin]")
{
	namespace fs = std::filesystem;

	auto base = (fs::temp_directory_path() / "thx_rotate_test.log").string();
	auto rot1 = base + ".1";

	fs::remove(base);
	fs::remove(rot1);

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	// max 64 bytes so a handful of writes forces a rotation.
	auto rb = svc->makeRotatingFileBackend(base.c_str(), 64, 3);
	REQUIRE(rb != nullptr);
	svc->addBackend(rb);

	// Each write is ~20 bytes; four writes push us past 64.
	for (int i = 0; i < 8; ++i)
		svc->log(LogLevel::Info, "rotate-test-line");

	rb.reset();

	bool base_exists = fs::exists(base);
	bool rot1_exists = fs::exists(rot1);

	fs::remove(base);
	fs::remove(rot1);
	fs::remove(base + ".2");
	fs::remove(base + ".3");

	REQUIRE(base_exists);
	REQUIRE(rot1_exists);
}

TEST_CASE("LoggingPlugin - rotating backend with invalid args returns null",
          "[logging_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	REQUIRE(svc->makeRotatingFileBackend(nullptr,   64, 3) == nullptr);
	REQUIRE(svc->makeRotatingFileBackend("x.log",    0, 3) == nullptr);
	REQUIRE(svc->makeRotatingFileBackend("x.log",   64, 0) == nullptr);
}
