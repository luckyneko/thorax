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

#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include "interfaces/file_service.h"
#include "interfaces/logging_service.h"

#include <cstdio>
#include <fstream>

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir>\n", argv[0]);
		return 1;
	}

	// Free-function facades forward to the Registry-owned managers.
	auto summary = thx::plugin::discoverAndLoad(argv[1]);
	if (summary.loaded.empty())
	{
		std::fprintf(stderr, "discoverAndLoad: no plugins loaded from %s\n", argv[1]);
		for (auto const& [path, err] : summary.failed)
			std::fprintf(stderr, "  %s: %s\n", path.c_str(), err.message.c_str());
		return 1;
	}

	auto log_svc = thx::service::getService<examples::ILoggingService>();
	if (!log_svc)
	{
		std::fprintf(stderr, "ILoggingService not found\n");
		return 1;
	}

	auto file_svc = thx::service::getService<examples::IFileService>();
	if (!file_svc)
	{
		std::fprintf(stderr, "IFileService not found\n");
		return 1;
	}

	log_svc->log("Services loaded.");

	// Write a small probe file and read it back via FileService.
	const char* tmp_path = "thorax_example.tmp";
	{
		std::ofstream f(tmp_path);
		if (!f)
		{
			std::fprintf(stderr, "Cannot write temp file\n");
			return 1;
		}
		f << "Hello from thorax FileService!";
	}

	char buf[64] = {};
	int n = file_svc->read(tmp_path, buf, static_cast<int>(sizeof(buf)));
	if (n < 0)
	{
		std::fprintf(stderr, "FileService::read failed\n");
		return 1;
	}

	log_svc->log(buf);
	log_svc->log("Done.");
	return 0;
}
