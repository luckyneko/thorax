/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: choose a logging backend at runtime.
//
// Usage: host_logging <plugin-dir>   (a dir containing logger plugins)
//
// Logging in thorax is just a service: thx::log::* forwards to the registered
// thx::log::ILogService, falling back to stderr when none is registered. A
// logger's identity is its plugin, so the available loggers are exactly the
// plugins that *provide* ILogService — discoverable without loading any DSO.
// This host emits a line (stderr fallback), then discovers + selects + loads a
// logger plugin, and emits again (now routed through it).

#include <thx/lifecycle.h>
#include <thx/log/log.h>
#include <thx/log/log_service.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include <cstdio>

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir>\n", argv[0]);
		return 1;
	}

	thx::initialise("host_logging");

	// No logger registered yet — this goes to the built-in stderr fallback.
	thx::log::info("before: routed to the stderr fallback");

	if (auto r = thx::plugin::discover(argv[1]); !r)
	{
		std::fprintf(stderr, "discover failed: %s\n", r.error().message.c_str());
		return 1;
	}

	// The available loggers are the plugins that provide ILogService. No DSO is
	// mapped to enumerate them — this reads the discovered manifests.
	auto loggers = thx::plugin::pluginsProviding<thx::log::ILogService>();
	std::printf("available loggers (%zu):\n", loggers.size());
	for (auto const& l : loggers)
		std::printf("  %s @ %u.%u.%u\n", l.name.c_str(), l.version.major, l.version.minor, l.version.patch);

	if (loggers.empty())
	{
		std::fprintf(stderr, "no ILogService providers discovered in %s\n", argv[1]);
		thx::shutdown();
		return 1;
	}

	// Pick one (here: the first) and load it. Single-owner — one active logger.
	auto const& chosen = loggers.front();
	std::printf("selecting '%s'\n", chosen.name.c_str());
	if (auto r = thx::plugin::load(chosen.path); !r)
	{
		std::fprintf(stderr, "load failed: %s\n", r.error().message.c_str());
		thx::shutdown();
		return 1;
	}

	int rc = thx::service::getService<thx::log::ILogService>() ? 0 : 1;
	if (rc != 0)
		std::fprintf(stderr, "ILogService not registered after load\n");

	// Now routed through the chosen logger (e.g. spdlog), not the fallback.
	thx::log::info("after: routed through the loaded logger");

	thx::shutdown();
	return rc;
}
