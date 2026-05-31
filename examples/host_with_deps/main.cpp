/*
 *  Created by LuckyNeko on 31/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: load a plugin together with its dependencies.
//
// Usage: host_with_deps <plugin-dir>
//
// The greeter plugin declares (in its manifest / IPlugin::required()) that it
// needs ILoggingService. Loading it directly would fail unless a logging
// provider were already registered. loadWithDependencies() reads the manifest
// requirements, resolves a provider for each from the discovered set, and
// loads everything in dependency order — so the logging plugin comes up first.

#include <thx/lifecycle.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include "interfaces/greeter_service.h"

#include <cstdio>

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir>\n", argv[0]);
		return 1;
	}

	if (auto r = thx::plugin::discover(argv[1]); !r)
	{
		std::fprintf(stderr, "discover failed: %s\n", r.error().message.c_str());
		return 1;
	}

	// Find the greeter by name, then load it *and* its dependency closure.
	auto greeter = thx::plugin::pluginByName("examples.GreeterPlugin");
	if (!greeter)
	{
		std::fprintf(stderr, "greeter plugin not discovered\n");
		return 1;
	}

	auto summary = thx::plugin::loadWithDependencies(greeter->path);
	if (!summary.failed.empty())
	{
		std::fprintf(stderr, "loadWithDependencies had failures:\n");
		for (auto const& [path, err] : summary.failed)
			std::fprintf(stderr, "  %s: %s\n", path.c_str(), err.message.c_str());
		return 1;
	}
	std::printf("Loaded %zu plugin(s) in dependency order:\n", summary.loaded.size());
	for (auto const& p : summary.loaded)
		std::printf("  %s\n", p.c_str());

	int rc = 0;
	{
		// The greeter routes its greeting through the logging service that
		// loadWithDependencies pulled in automatically.
		auto svc = thx::service::getService<examples::IGreeterService>();
		if (svc)
			svc->greet("thorax");
		else
		{
			std::fprintf(stderr, "IGreeterService not found after load\n");
			rc = 1;
		}
	}

	thx::shutdown();
	return rc;
}
