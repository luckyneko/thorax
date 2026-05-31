/*
 *  Created by LuckyNeko on 31/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: load every plugin that provides a given service interface.
//
// Usage: host_by_provides <plugin-dir>
//
// pluginsProviding<T>() filters the discovered manifests by their `provides`
// list (no DSO opened), and loadAll() loads the whole set — pulling in each
// one's dependencies in order.

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

	if (auto r = thx::plugin::discover(argv[1]); !r)
	{
		std::fprintf(stderr, "discover failed: %s\n", r.error().message.c_str());
		return 1;
	}

	// Pick out every plugin advertising ILoggingService in its manifest.
	auto providers = thx::plugin::pluginsProviding<examples::ILoggingService>();
	if (providers.empty())
	{
		std::fprintf(stderr, "no plugin provides %s\n",
					 examples::ILoggingService::staticId().name());
		return 1;
	}
	std::printf("Found %zu provider(s) of %s:\n", providers.size(),
				examples::ILoggingService::staticId().name());
	for (auto const& p : providers)
		std::printf("  %s\n", p.path.c_str());

	// Load them all (and anything they depend on) in dependency order.
	// loadAll takes a thx::Span (the ABI-stable view type), built here from
	// the vector pluginsProviding returned.
	auto summary = thx::plugin::loadAll({providers.data(), providers.size()});
	if (summary.loaded.empty())
	{
		std::fprintf(stderr, "loadAll loaded nothing\n");
		for (auto const& [path, err] : summary.failed)
			std::fprintf(stderr, "  %s: %s\n", path.c_str(), err.message.c_str());
		return 1;
	}

	int rc = 0;
	{
		auto log = thx::service::getService<examples::ILoggingService>();
		if (log)
			log->log("Loaded all providers of ILoggingService.");
		else
		{
			std::fprintf(stderr, "ILoggingService not found after loadAll\n");
			rc = 1;
		}
	}

	thx::shutdown();
	return rc;
}
