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
// Mechanism: dlopens the DSO via PluginHandle, instantiates the IPlugin,
// reads name() / version() / required() / provides() (all const, no side
// effects by contract — onLoad() is NOT called), and asks the library to
// serialise the data via thx::plugin::serialiseManifest. The IPlugin is
// destroyed before the file is written; the DSO is queued to PluginGarbage
// on PluginHandle teardown and reclaimed by the OS at exit.

#include "library.h"
#include "thx/plugin/iplugin.h"
#include "thx/plugin/manifest.h"
#include "plugin/plugin_handle.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{

std::string manifestPathForDso(std::string const& dsoPath)
{
	std::string const suffix = thx::LIBRARY_EXTENSION;
	if (dsoPath.size() > suffix.size()
	    && dsoPath.compare(dsoPath.size() - suffix.size(), suffix.size(), suffix) == 0)
		return dsoPath.substr(0, dsoPath.size() - suffix.size()) + ".thx.json";
	return dsoPath + ".thx.json";
}

// Build a PluginManifest from a live IPlugin. Mirrors what discover() would
// produce if the manifest had been hand-authored to match the IPlugin's
// declarations.
thx::plugin::PluginManifest manifestFromPlugin(thx::plugin::IPlugin const& plugin)
{
	thx::plugin::PluginManifest m;
	m.schema  = 1;
	m.name    = std::string(static_cast<std::string_view>(plugin.name()));
	m.version = plugin.version();

	auto provides = plugin.provides();
	m.provides.reserve(provides.size());
	for (std::size_t i = 0; i < provides.size(); ++i)
		m.provides.emplace_back(provides[i].name());

	auto reqs = plugin.required();
	m.requirements.reserve(reqs.size());
	for (std::size_t i = 0; i < reqs.size(); ++i)
		m.requirements.push_back({std::string(reqs[i].id.name()), reqs[i].version});

	return m;
}

int emit(std::string const& dsoPath, std::string const& outPath)
{
	auto handleResult = thx::plugin::PluginHandle::open(dsoPath);
	if (!handleResult)
	{
		std::fprintf(stderr, "thx_emit_manifest: cannot open '%s': %s\n",
		             dsoPath.c_str(), handleResult.error().message.c_str());
		return 1;
	}

	auto handle  = std::move(handleResult.value());
	auto* create  = handle.createFn();
	auto* destroy = handle.destroyFn();

	auto* plugin = create();
	if (!plugin)
	{
		std::fprintf(stderr, "thx_emit_manifest: thx_create_plugin returned null for '%s'\n",
		             dsoPath.c_str());
		return 1;
	}

	std::string json = thx::plugin::serialiseManifest(manifestFromPlugin(*plugin));

	// Tear the plugin down before writing — the destroy must run while the
	// DSO is still mapped, and we have no further need of the IPlugin.
	destroy(plugin);

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
