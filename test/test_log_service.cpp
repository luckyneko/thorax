/*
 *  Created by LuckyNeko on 06/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/log.h>
#include <thx/log/log_service.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include <filesystem>
#include <string>
#include <vector>

#ifndef THX_LOG_SERVICE_PLUGIN_PATH
#	error "THX_LOG_SERVICE_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif
#ifndef THX_SPDLOG_PLUGIN_PATH
#	error "THX_SPDLOG_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// plugin_log_service is the bridge between core's plain thx::LogSink and
// the service-based ILogService world: on load it installs a sink that forwards
// every core diagnostic to the registered ILogService; on unload it clears the
// sink. It registers no service and `requires` thx.log.ILogService, so
// loadWithDependencies pulls in a backend (the same shape io's file handler has
// on the IIoService provider). The reset listener unloads everything and drains
// the DSO graveyard after each case.

namespace
{
	// A host-side ILogService that records the messages it receives, so we can
	// observe what the bridge forwards. Registers under thx.log.ILogService (the
	// CRTP id is rooted at the interface), satisfying the bridge's requirement.
	struct CaptureLog : thx::log::ILogService
	{
		std::vector<std::string> messages;
		void write(thx::log::LogRecord const& r) override
		{
			messages.emplace_back(r.message.data(), r.message.size());
		}
	};

	CaptureLog* installCapture()
	{
		CaptureLog* cap = nullptr;
		REQUIRE(thx::service::registerService<CaptureLog>(
			[&]() -> thx::service::IService*
			{
				cap = new CaptureLog();
				return cap;
			}));
		REQUIRE(cap != nullptr);
		return cap;
	}
} // namespace

TEST_CASE("log bridge - forwards core thx::log to a registered ILogService", "[log_service]")
{
	auto* cap = installCapture();

	// The bridge requires thx.log.ILogService; our capture satisfies it.
	REQUIRE(thx::plugin::load(THX_LOG_SERVICE_PLUGIN_PATH));

	thx::logMessage(thx::LogLevel::Info, "through the bridge");

	REQUIRE_FALSE(cap->messages.empty());
	REQUIRE(cap->messages.back() == "through the bridge");
}

TEST_CASE("log bridge - load fails when no ILogService is registered", "[log_service]")
{
	// The bridge's required() is unmet — no backend and no host service.
	REQUIRE(thx::service::getService<thx::log::ILogService>() == nullptr);
	REQUIRE_FALSE(thx::plugin::load(THX_LOG_SERVICE_PLUGIN_PATH));
}

TEST_CASE("log bridge - unloading clears the sink (core logging falls back)", "[log_service]")
{
	auto* cap = installCapture();

	REQUIRE(thx::plugin::load(THX_LOG_SERVICE_PLUGIN_PATH));
	thx::logMessage(thx::LogLevel::Info, "one");
	REQUIRE(cap->messages.size() == 1);

	REQUIRE(thx::plugin::unload(THX_LOG_SERVICE_PLUGIN_PATH));
	thx::plugin::collectGarbage();

	// Sink cleared → core falls back to stderr; the capture sees nothing more.
	thx::logMessage(thx::LogLevel::Info, "two");
	REQUIRE(cap->messages.size() == 1);
}

TEST_CASE("log bridge - loadWithDependencies pulls in a logger backend", "[log_service][integration]")
{
	namespace fs = std::filesystem;
	REQUIRE(thx::plugin::discover(fs::path(THX_LOG_SERVICE_PLUGIN_PATH).parent_path().string()));
	REQUIRE(thx::plugin::discover(fs::path(THX_SPDLOG_PLUGIN_PATH).parent_path().string()));

	// The bridge requires ILogService; resolveLoadOrder finds the spdlog backend
	// (it provides that id) and loads it first, then the bridge.
	auto summary = thx::plugin::loadWithDependencies(THX_LOG_SERVICE_PLUGIN_PATH);
	REQUIRE(summary.failed.empty());
	REQUIRE(summary.loaded.size() == 2); // backend + bridge

	REQUIRE(thx::service::getService<thx::log::ILogService>() != nullptr);

	// Core logging now flows host → core LogSink → bridge → spdlog ILogService.
	REQUIRE_NOTHROW(thx::logMessage(thx::LogLevel::Info, "routed through spdlog via the bridge"));
}
