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
// effects by contract — onLoad() is NOT called), serialises to JSON,
// destroys the IPlugin, exits. The DSO is queued to the process-wide
// PluginGarbage on PluginHandle teardown and reclaimed by the OS at exit.

#include "thx/library.h"
#include "thx/plugin/iplugin.h"
#include "thx/plugin/plugin_handle.h"
#include "thx/to_string.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace
{

std::string escapeJsonString(std::string_view s)
{
	std::string out;
	out.reserve(s.size() + 2);
	for (char c : s)
	{
		switch (c)
		{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			default:   out += c;
		}
	}
	return out;
}

std::string manifestPathForDso(std::string const& dsoPath)
{
	std::string const suffix = thx::LIBRARY_EXTENSION;
	if (dsoPath.size() > suffix.size()
	    && dsoPath.compare(dsoPath.size() - suffix.size(), suffix.size(), suffix) == 0)
		return dsoPath.substr(0, dsoPath.size() - suffix.size()) + ".thx.json";
	return dsoPath + ".thx.json";
}

// Build the manifest JSON for `plugin`. Pure function — does not touch
// the filesystem.
std::string buildManifestJson(thx::plugin::IPlugin const& plugin)
{
	auto provides = plugin.provides();
	auto reqs     = plugin.required();

	std::ostringstream out;
	out << "{\n";
	out << "  \"schema\":   1,\n";
	out << "  \"name\":     \""
	    << escapeJsonString(static_cast<std::string_view>(plugin.name()))
	    << "\",\n";
	out << "  \"version\":  \"" << thx::toString(plugin.version()) << "\",\n";

	out << "  \"provides\": [";
	if (provides.size() > 0)
	{
		out << "\n";
		for (std::size_t i = 0; i < provides.size(); ++i)
		{
			out << "    \"" << escapeJsonString(provides[i].name()) << "\"";
			if (i + 1 < provides.size()) out << ",";
			out << "\n";
		}
		out << "  ";
	}
	out << "],\n";

	out << "  \"requires\": [";
	if (reqs.size() > 0)
	{
		out << "\n";
		for (std::size_t i = 0; i < reqs.size(); ++i)
		{
			out << "    {\"id\": \"" << escapeJsonString(reqs[i].id.name())
			    << "\", \"version\": \"" << thx::toString(reqs[i].version) << "\"}";
			if (i + 1 < reqs.size()) out << ",";
			out << "\n";
		}
		out << "  ";
	}
	out << "]\n";
	out << "}\n";
	return out.str();
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
	auto* create = handle.createFn();
	auto* destroy = handle.destroyFn();

	auto* plugin = create();
	if (!plugin)
	{
		std::fprintf(stderr, "thx_emit_manifest: thx_create_plugin returned null for '%s'\n",
		             dsoPath.c_str());
		return 1;
	}

	std::string json = buildManifestJson(*plugin);

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
