/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Example host application demonstrating the thorax plugin framework.
//
// Usage: example_host <plugin-dir>
//
// Loads all plugins found in <plugin-dir>, then exercises the LoggingService
// and FileService interfaces contributed by the two example plugins.

#include <thx/plugin_loader.h>
#include <thx/service_manager.h>

#include "interfaces/file_service.h"
#include "interfaces/logging_service.h"

#include <cstdio>

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir>\n", argv[0]);
		return 1;
	}

	auto& sm = thx::ServiceManager::instance();
	thx::PluginLoader loader(sm);

	auto summary = loader.discover_and_load(argv[1]);
	if (summary.loaded.empty())
	{
		std::fprintf(stderr, "discover_and_load: no plugins loaded from %s\n", argv[1]);
		for (auto const& [path, err] : summary.failed)
			std::fprintf(stderr, "  %s: %s\n", path.c_str(), err.message.c_str());
		return 1;
	}

	auto log_svc = sm.get_service<examples::ILoggingService>();
	if (!log_svc)
	{
		std::fprintf(stderr, "ILoggingService not found\n");
		return 1;
	}

	auto file_svc = sm.get_service<examples::IFileService>();
	if (!file_svc)
	{
		std::fprintf(stderr, "IFileService not found\n");
		return 1;
	}

	log_svc->log("Services loaded.");

	// Write a small probe file and read it back via FileService.
	const char* tmp_path = "thorax_example.tmp";
	{
		FILE* f = std::fopen(tmp_path, "w");
		if (!f)
		{
			std::fprintf(stderr, "Cannot write temp file\n");
			return 1;
		}
		std::fputs("Hello from thorax FileService!", f);
		std::fclose(f);
	}

	char buf[64] = {};
	int  n       = file_svc->read(tmp_path, buf, static_cast<int>(sizeof(buf)));
	if (n < 0)
	{
		std::fprintf(stderr, "FileService::read failed\n");
		return 1;
	}

	log_svc->log(buf);
	log_svc->log("Done.");
	return 0;
}
