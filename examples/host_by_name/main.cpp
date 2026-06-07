/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: select a plugin by its manifest name, then load it with its deps.
//
// Usage: host_by_name <plugin-dir>
//
// discover() reads every sidecar manifest in the directory (no DSO is opened),
// so we can look a plugin up by name. The image-decoder plugin requires
// IAssetService, so we use loadWithDependencies() to pull the media-core plugin
// in automatically.

#include <thx/log.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>
#include <thx/thorax.h>

#include "interfaces/asset_service.h"

#include <cstdio>

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir>\n", argv[0]);
		return 1;
	}

	thx::Settings settings;
	settings.name = "host_by_name";
	thx::initialise(settings);

	// Populate the manifest cache for the directory. No DSO is mapped yet.
	if (auto r = thx::plugin::discover(argv[1]); !r)
	{
		std::fprintf(stderr, "discover failed: %s\n", r.error().message.c_str());
		return 1;
	}

	// A custom-IPlugin plugin carries its own manifest name (distinct from any
	// service id). Pick the image decoder by that name.
	const char* wanted = "examples.media.ImageDecoder";
	auto info = thx::plugin::pluginByName(wanted);
	if (!info)
	{
		std::fprintf(stderr, "no plugin named '%s' was discovered\n", wanted);
		return 1;
	}
	std::printf("Found '%s' at %s\n", info->name.c_str(), info->path.c_str());

	// loadWithDependencies pulls in media-core (the decoder's required service).
	auto summary = thx::plugin::loadWithDependencies(info->path);
	if (!summary.failed.empty())
	{
		for (auto const& [path, err] : summary.failed)
			std::fprintf(stderr, "load failed: %s: %s\n", path.c_str(), err.message.c_str());
		thx::shutdown();
		return 1;
	}

	int rc = 0;
	{
		auto media = thx::service::getService<examples::IAssetService>();
		examples::AssetInfo asset;
		if (media && media->decode("portrait.jpg", asset))
			thx::logMessage(thx::LogLevel::Info, "decoded portrait.jpg via the named decoder");
		else
		{
			std::fprintf(stderr, "decode failed after load-by-name\n");
			rc = 1;
		}
		// Handle drops here, before shutdown() unmaps the plugin DSOs.
	}

	thx::shutdown();
	return rc;
}
