/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/log/log_service.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include <filesystem>
#include <string>

#ifndef THX_SPDLOG_PLUGIN_PATH
#	error "THX_SPDLOG_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// The spdlog plugin is a *backend*: it registers an spdlog-backed
// thx::log::ILogService (the interface now lives in log_interface, not core)
// under the stable interface id, carrying its own selection identity in its
// plugin manifest ("thx.spdlog.SpdlogService"). Core diagnostics only reach it
// once plugin_log_service bridges core's LogSink to the registered ILogService
// (see test_log_service.cpp); these cases exercise the backend's own service
// registration. The reset listener unloads the plugin after each case.

namespace
{
	constexpr const char* kPluginName = "thx.spdlog.SpdlogService";
}

TEST_CASE("spdlog backend - loads and registers an ILogService", "[spdlog_plugin]")
{
	// Nothing registered up front.
	REQUIRE(thx::service::getService<thx::log::ILogService>() == nullptr);

	REQUIRE(thx::plugin::load(THX_SPDLOG_PLUGIN_PATH));

	auto svc = thx::service::getService<thx::log::ILogService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc->id() == thx::log::ILogService::staticId());
}

TEST_CASE("spdlog backend - the registered service accepts records", "[spdlog_plugin]")
{
	REQUIRE(thx::plugin::load(THX_SPDLOG_PLUGIN_PATH));
	auto svc = thx::service::getService<thx::log::ILogService>();
	REQUIRE(svc != nullptr);

	// Call the service directly (host → libthorax → plugin DSO → spdlog). We
	// can't capture spdlog's stderr output in-process, so we assert the path is
	// safe end-to-end under the sanitizers.
	thx::log::LogRecord rec{thx::log::LogLevel::Info, thx::rtti::SourceLocation::current(),
							thx::StringView{"direct to the backend"}};
	REQUIRE_NOTHROW(svc->write(rec));
}

TEST_CASE("spdlog backend - unloading unregisters the service", "[spdlog_plugin]")
{
	REQUIRE(thx::plugin::load(THX_SPDLOG_PLUGIN_PATH));
	REQUIRE(thx::service::getService<thx::log::ILogService>() != nullptr);

	REQUIRE(thx::plugin::unload(THX_SPDLOG_PLUGIN_PATH));

	REQUIRE(thx::service::getService<thx::log::ILogService>() == nullptr);
}

TEST_CASE("spdlog backend - discoverable as an ILogService provider by name", "[spdlog_plugin]")
{
	namespace fs = std::filesystem;
	auto dir = fs::path(THX_SPDLOG_PLUGIN_PATH).parent_path().string();

	REQUIRE(thx::plugin::discover(dir));

	// The logger's selection identity lives on the plugin manifest, distinct
	// from the interface id it registers the service under.
	auto providers = thx::plugin::pluginsProviding<thx::log::ILogService>();
	REQUIRE_FALSE(providers.empty());
	bool found = false;
	for (const auto& p : providers)
		if (p.name == kPluginName)
			found = true;
	REQUIRE(found);

	// Pick it by name and load by its path.
	auto chosen = thx::plugin::pluginByName(kPluginName);
	REQUIRE(chosen.has_value());
	REQUIRE(thx::plugin::load(chosen->path));
	REQUIRE(thx::service::getService<thx::log::ILogService>() != nullptr);
}

TEST_CASE("spdlog backend - single-owner: a host ILogService blocks the plugin's",
		  "[spdlog_plugin]")
{
	// Register a host-side ILogService first; the plugin's onLoad registration
	// must then fail the load (single-owner registry), leaving ours in place.
	struct HostService : thx::log::ILogService
	{
		int count = 0;
		void write(const thx::log::LogRecord&) override { ++count; }
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

	// Our service is still the registered one.
	auto svc = thx::service::getService<thx::log::ILogService>();
	REQUIRE(svc != nullptr);
	REQUIRE(svc.get() == static_cast<thx::log::ILogService*>(host));
}
