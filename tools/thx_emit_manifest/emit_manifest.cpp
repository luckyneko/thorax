/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// thx_emit_manifest — build-time tool that derives a *.thx.json sidecar
// directly from a built plugin DSO.
//
//   usage: thx_emit_manifest <dso-path> [output-path]
//
// If <output-path> is omitted, writes to <basename>.thx.json next to the
// DSO (matching the discover() pairing rule). Otherwise writes to the
// given path.
//
// Mechanism: calls thx::plugin::inspect() — the public manifest-less
// inspection entry point — which opens the DSO, instantiates the IPlugin,
// reads its metadata, tears it down, then queues the DSO for deferred
// close. The returned PluginManifest is serialised via the public
// thx::plugin::serialiseManifest() and written to disk.
//
// This tool is a regular external consumer of libthorax's public API;
// it doesn't link against any internal headers.

#include "thx/plugin/manifest.h"
#include "thx/plugin/plugin.h"

#include <cstdio>
#include <fstream>
#include <string>

namespace
{

	// Mirrors discover()'s sidecar↔DSO pairing rule: strip the platform DSO
	// suffix and append .thx.json. The platform extension is duplicated here
	// rather than imported from the framework's private library.h header —
	// the tool is a normal API consumer and shouldn't reach into src/.
	std::string manifestPathForDso(std::string const& dsoPath)
	{
#if defined(_WIN32)
		constexpr char const* kExt = ".dll";
#elif defined(__APPLE__)
		constexpr char const* kExt = ".dylib";
#else
		constexpr char const* kExt = ".so";
#endif
		std::string const suffix = kExt;
		if (dsoPath.size() > suffix.size() && dsoPath.compare(dsoPath.size() - suffix.size(), suffix.size(), suffix) == 0)
			return dsoPath.substr(0, dsoPath.size() - suffix.size()) + ".thx.json";
		return dsoPath + ".thx.json";
	}

	int emit(std::string const& dsoPath, std::string const& outPath)
	{
		auto inspected = thx::plugin::inspect(dsoPath);
		if (!inspected)
		{
			std::fprintf(stderr, "thx_emit_manifest: %s\n",
						 inspected.error().message.c_str());
			return 1;
		}

		auto json = thx::plugin::serialiseManifest(inspected.value());

		std::ofstream f(outPath);
		if (!f)
		{
			std::fprintf(stderr, "thx_emit_manifest: cannot write '%s'\n", outPath.c_str());
			return 1;
		}
		f << json;
		return 0;
	}

} // namespace

int main(int argc, char* argv[])
{
	if (argc < 2 || argc > 3)
	{
		std::fprintf(stderr, "usage: thx_emit_manifest <dso-path> [output-path]\n");
		return 2;
	}
	std::string dsoPath = argv[1];
	std::string outPath = (argc == 3) ? argv[2] : manifestPathForDso(dsoPath);
	return emit(dsoPath, outPath);
}
