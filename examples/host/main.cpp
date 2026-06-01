/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// The tour. Loads the whole media plugin set from a directory and decodes a few
// assets through the resulting IAssetService.
//
// Usage: example_host <plugin-dir>
//
// Note we use loadAll() (dependency-aware) rather than discoverAndLoad(): the
// decoder plugins require IAssetService, so they must come up after the
// media-core plugin. loadAll() resolves that order from the manifests;
// discoverAndLoad() would load in filesystem order and fail the decoders.

#include <thx/lifecycle.h>
#include <thx/log/log.h>
#include <thx/plugin/plugin.h>
#include <thx/service/service.h>

#include "interfaces/asset_service.h"

#include <cstdio>
#include <string>

namespace
{
	const char* kindName(examples::AssetKind k)
	{
		switch (k)
		{
			case examples::AssetKind::Image:
				return "image";
			case examples::AssetKind::Video:
				return "video";
			default:
				return "unknown";
		}
	}

	void report(examples::IAssetService& media, const char* path)
	{
		examples::AssetInfo info;
		if (media.decode(path, info))
			thx::log::info(std::string("decoded ") + path + ": " + kindName(info.kind) + " " + std::to_string(info.width) + "x" + std::to_string(info.height) + (info.durationMs ? " " + std::to_string(info.durationMs) + "ms" : ""));
		else
			thx::log::warn(std::string("could not decode ") + path);
	}
} // namespace

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir>\n", argv[0]);
		return 1;
	}

	thx::initialise("example_host");

	if (auto r = thx::plugin::discover(argv[1]); !r)
	{
		std::fprintf(stderr, "discover failed: %s\n", r.error().message.c_str());
		return 1;
	}

	// Load every discovered plugin, in dependency order (core before decoders).
	auto discovered = thx::plugin::plugins();
	auto summary = thx::plugin::loadAll({discovered.data(), discovered.size()});
	if (!summary.failed.empty())
	{
		for (auto const& [path, err] : summary.failed)
			std::fprintf(stderr, "load failed: %s: %s\n", path.c_str(), err.message.c_str());
		thx::shutdown();
		return 1;
	}
	thx::log::info("loaded " + std::to_string(summary.loaded.size()) + " plugin(s)");

	int rc = 0;
	{
		auto media = thx::service::getService<examples::IAssetService>();
		if (!media)
		{
			std::fprintf(stderr, "IAssetService not found after load\n");
			rc = 1;
		}
		else
		{
			report(*media, "photo.png");
			report(*media, "clip.mp4");
			report(*media, "notes.txt"); // no decoder — expected miss
		}
	}

	thx::shutdown();
	return rc;
}
