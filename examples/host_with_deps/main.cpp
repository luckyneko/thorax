/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: load a plugin together with its dependency closure, in order.
//
// Usage: host_with_deps <plugin-dir>
//
// The image-decoder plugin declares (in its manifest / IPlugin::required()) that
// it needs IAssetService. Loading it directly would fail unless a provider were
// already registered. loadWithDependencies() reads the requirements, resolves a
// provider for each from the discovered set, and loads everything in dependency
// order — so the media-core plugin comes up first.

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
	settings.name = "host_with_deps";
	thx::initialise(settings);

	if (auto r = thx::plugin::discover(argv[1]); !r)
	{
		std::fprintf(stderr, "discover failed: %s\n", r.error().message.c_str());
		return 1;
	}

	auto decoder = thx::plugin::pluginByName("examples.media.VideoDecoder");
	if (!decoder)
	{
		std::fprintf(stderr, "video decoder plugin not discovered\n");
		return 1;
	}

	// Load the decoder *and* its dependency closure (media-core) in order.
	auto summary = thx::plugin::loadWithDependencies(decoder->path);
	if (!summary.failed.empty())
	{
		for (auto const& [path, err] : summary.failed)
			std::fprintf(stderr, "  %s: %s\n", path.c_str(), err.message.c_str());
		thx::shutdown();
		return 1;
	}
	std::printf("Loaded %zu plugin(s) in dependency order:\n", summary.loaded.size());
	for (auto const& p : summary.loaded)
		std::printf("  %s\n", p.c_str());

	int rc = 0;
	{
		auto media = thx::service::getService<examples::IAssetService>();
		examples::AssetInfo asset;
		if (media && media->decode("trailer.mp4", asset))
			thx::logMessage(thx::LogLevel::Info, "decoded trailer.mp4 after dependency-ordered load");
		else
		{
			std::fprintf(stderr, "decode failed after loadWithDependencies\n");
			rc = 1;
		}
	}

	thx::shutdown();
	return rc;
}
