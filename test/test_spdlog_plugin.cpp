/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/log/log.h>
#include <thx/log/log_service.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include <filesystem>
#include <string>

#ifndef THX_SPDLOG_PLUGIN_PATH
#	error "THX_SPDLOG_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// The spdlog plugin registers an spdlog-backed thx::log::ILogService under the
// stable interface id, while carrying its own selection identity in its plugin
// manifest ("thx.spdlog.SpdlogService"). Once loaded, thx::log::* forwards every
// diagnostic to it; unloaded, logging falls back to the built-in stderr writer.
// The test-wide reset listener unloads the plugin (and unregisters the service)
// after each case.

namespace
{
	constexpr const char* kPluginName = "thx.spdlog.SpdlogService";
}

TEST_CASE("SpdlogPlugin - loads and registers an ILogService", "[spdlog_plugin]")
{
	// Nothing registered up front: logging uses the stderr fallback.
	REQUIRE(thx::service::getService<thx::log::ILogService>() == nullptr);

	REQUIRE(thx::plugin::load(THX_SPDLOG_PLUGIN_PATH));

	auto svc = thx::service::getService<thx::log::ILogService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->id() == thx::log::ILogService::staticId());
}

TEST_CASE("SpdlogPlugin - thx::log routes through the plugin's service", "[spdlog_plugin]")
{
	REQUIRE(thx::plugin::load(THX_SPDLOG_PLUGIN_PATH));
	REQUIRE(thx::service::getService<thx::log::ILogService>() != nullptr);

	// These now route to the plugin's spdlog logger (stderr). We can't capture
	// spdlog's output in-process, so we assert the calls are safe end-to-end —
	// host → libthorax → plugin DSO → spdlog — under the sanitizers.
	REQUIRE_NOTHROW(thx::log::debug("debug via spdlog"));
	REQUIRE_NOTHROW(thx::log::info("info via spdlog"));
	REQUIRE_NOTHROW(thx::log::warn("warn via spdlog"));
	REQUIRE_NOTHROW(thx::log::error("error via spdlog"));
}

TEST_CASE("SpdlogPlugin - unloading restores the stderr fallback", "[spdlog_plugin]")
{
	REQUIRE(thx::plugin::load(THX_SPDLOG_PLUGIN_PATH));
	REQUIRE(thx::service::getService<thx::log::ILogService>() != nullptr);

	REQUIRE(thx::plugin::unload(THX_SPDLOG_PLUGIN_PATH));

	REQUIRE(thx::service::getService<thx::log::ILogService>() == nullptr);
	REQUIRE_NOTHROW(thx::log::info("back to stderr fallback"));
}

// ---------------------------------------------------------------------------
// Selection flow (Option A): a host discovers the loggers that provide
// ILogService and picks one by its distinct plugin identity, then loads it.
// ---------------------------------------------------------------------------

TEST_CASE("SpdlogPlugin - discoverable as an ILogService provider by name", "[spdlog_plugin]")
{
	namespace fs = std::filesystem;
	auto dir = fs::path(THX_SPDLOG_PLUGIN_PATH).parent_path().string();

	REQUIRE(thx::plugin::discover(dir));

	// The logger's selection identity lives on the plugin manifest, distinct
	// from the interface id it registers the service under.
	auto providers = thx::plugin::pluginsProviding<thx::log::ILogService>();
	REQUIRE_FALSE(providers.empty());
	bool found = false;
	for (auto const& p : providers)
		if (p.name == kPluginName)
			found = true;
	REQUIRE(found);

	// Pick it by name and load by its path; thx::log then routes to it.
	auto chosen = thx::plugin::pluginByName(kPluginName);
	REQUIRE(chosen.has_value());
	REQUIRE(thx::plugin::load(chosen->path));
	REQUIRE(thx::service::getService<thx::log::ILogService>() != nullptr);
}

TEST_CASE("SpdlogPlugin - single-owner: a host ILogService blocks the plugin's",
		  "[spdlog_plugin]")
{
	// Register a host-side ILogService first; the plugin's onLoad registration
	// must then fail the load (single-owner registry), leaving ours in place.
	struct HostService : thx::log::ILogService
	{
		int count = 0;
		void write(thx::log::LogRecord const&) override { ++count; }
	};
	HostService* host = nullptr;
	REQUIRE(thx::service::registerService<HostService>(
		[&]() -> thx::service::IService*
		{
			host = new HostService();
			return host;
		}));

	// load() should fail because onLoad's registerService hits the duplicate id.
	REQUIRE_FALSE(thx::plugin::load(THX_SPDLOG_PLUGIN_PATH));

	// Our service is still the registered one and still receives diagnostics.
	thx::log::info("still ours");
	REQUIRE(host->count >= 1);
}
