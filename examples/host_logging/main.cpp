/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: route framework logging through a plugin-provided backend.
//
// Usage: host_logging <plugin-dir> [more-dirs...]
//
// libthorax core logs through a plain thx::LogSink (stderr by default).
// The richer, service-based logging is NOT core: thx.log.ILogService lives in
// log_interface, a *backend* plugin (e.g. spdlog) implements & registers it, and
// the plugin_log_service *bridge* installs a LogSink that forwards every core
// diagnostic to that registered ILogService. The bridge `requires` ILogService,
// so loadWithDependencies(bridge) pulls a backend in and loads it first — the
// same shape host_io uses for the io provider/handler. This host emits a line
// (stderr fallback), then loads the bridge + a backend and emits again (now
// routed through the backend).

#include <thx/log.h>
#include <thx/log/log_service.h>
#include <thx/plugin/plugin.h>
#include <thx/thorax.h>

#include <cstdio>

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir> [more-dirs...]\n", argv[0]);
		return 1;
	}

	thx::Settings settings;
	settings.name = "host_logging";
	thx::initialise(settings);

	// No bridge loaded yet — this goes to core's built-in stderr fallback.
	thx::logMessage(thx::LogLevel::Info, "before: routed to the stderr fallback");

	// Discover every plugin directory passed on the command line. The in-tree
	// log plugins build into per-component dirs; an install co-locates them.
	for (int i = 1; i < argc; ++i)
	{
		if (auto r = thx::plugin::discover(argv[i]); !r)
		{
			std::fprintf(stderr, "discover(%s) failed: %s\n", argv[i], r.error().message.c_str());
			thx::shutdown();
			return 1;
		}
	}

	// The available logger backends are the plugins that provide ILogService. No
	// DSO is mapped to enumerate them — this reads the discovered manifests.
	auto loggers = thx::plugin::pluginsProviding<thx::log::ILogService>();
	std::printf("available logger backends (%zu):\n", loggers.size());
	for (auto const& l : loggers)
		std::printf("  %s @ %u.%u.%u\n", l.name.c_str(), l.version.major, l.version.minor, l.version.patch);
	if (loggers.empty())
	{
		std::fprintf(stderr, "no ILogService backend discovered\n");
		thx::shutdown();
		return 1;
	}

	// Load the bridge *with its dependencies*: it requires an ILogService, so
	// loadWithDependencies pulls a backend in and loads it first, then activates
	// the LogSink that routes core diagnostics through it.
	auto bridge = thx::plugin::pluginByName("thx.log.LogService");
	if (!bridge)
	{
		std::fprintf(stderr, "no log bridge (thx.log.LogService) discovered\n");
		thx::shutdown();
		return 1;
	}
	auto summary = thx::plugin::loadWithDependencies(bridge->path);
	if (!summary.failed.empty())
	{
		for (auto const& [p, err] : summary.failed)
			std::fprintf(stderr, "load failed: %s: %s\n", p.c_str(), err.message.c_str());
		thx::shutdown();
		return 1;
	}
	std::printf("loaded %zu plugin(s) (bridge + its logger backend)\n", summary.loaded.size());

	int rc = thx::service::getService<thx::log::ILogService>() ? 0 : 1;
	if (rc != 0)
		std::fprintf(stderr, "ILogService not registered after load\n");

	// Now routed through the loaded backend (e.g. spdlog), not the fallback.
	thx::logMessage(thx::LogLevel::Info, "after: routed through the loaded logger backend");

	thx::shutdown();
	return rc;
}
