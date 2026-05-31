/*
 *  Created by LuckyNeko on 31/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: load a single plugin by its manifest name.
//
// Usage: host_by_name <plugin-dir>
//
// discover() reads every sidecar manifest in the directory (no DSO is opened),
// so we can look a plugin up by name and load only that one.

#include <thx/lifecycle.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include "interfaces/logging_service.h"

#include <cstdio>

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir>\n", argv[0]);
		return 1;
	}

	// Populate the manifest cache for the directory. No DSO is mapped yet.
	if (auto r = thx::plugin::discover(argv[1]); !r)
	{
		std::fprintf(stderr, "discover failed: %s\n", r.error().message.c_str());
		return 1;
	}

	// A ServicePluginShim-based plugin reports its name as the service ID it
	// provides — here the logging plugin's name is "examples.ILoggingService".
	const char* wanted = examples::ILoggingService::staticId().name();
	auto info = thx::plugin::pluginByName(wanted);
	if (!info)
	{
		std::fprintf(stderr, "no plugin named '%s' was discovered\n", wanted);
		return 1;
	}
	std::printf("Found '%s' at %s\n", info->name.c_str(), info->path.c_str());

	if (auto r = thx::plugin::load(info->path); !r)
	{
		std::fprintf(stderr, "load failed: %s\n", r.error().message.c_str());
		return 1;
	}

	int rc = 0;
	{
		auto log = thx::service::getService<examples::ILoggingService>();
		if (log)
			log->log("Loaded by name.");
		else
		{
			std::fprintf(stderr, "ILoggingService not found after load\n");
			rc = 1;
		}
		// Handle drops here, before shutdown() unmaps the plugin DSO.
	}

	thx::shutdown();
	return rc;
}
