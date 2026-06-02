/*
 *  Created by LuckyNeko on 02/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

// Pathway: stream a file through the io subsystem.
//
// Usage: host_io <plugin-dir> [more-dirs...]
//
// io is NOT part of thorax core — it ships as plugins. The thx::io::open facade
// forwards to a registered IIoService (the scheme dispatcher) that a *provider*
// plugin supplies; *handler* plugins (file://, http://) contribute schemes to
// it. Until those are loaded, open() returns NoHandler. This host discovers the
// io plugins, shows open() failing before any are loaded, lists the discoverable
// IIoService provider, loads the subsystem in dependency order (the file handler
// requires the provider), then round-trips a file through the facade.

#include <thx/io/io.h>
#include <thx/io/stream.h>
#include <thx/lifecycle.h>
#include <thx/plugin/plugin.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
	thx::Span<const std::uint8_t> bytesOf(std::string const& s)
	{
		return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
	}
} // namespace

int main(int argc, char* argv[])
{
	if (argc < 2)
	{
		std::fprintf(stderr, "Usage: %s <plugin-dir> [more-dirs...]\n", argv[0]);
		return 1;
	}

	thx::initialise("host_io");

	const std::string path = (std::filesystem::temp_directory_path() / "thx_host_io.bin").string();
	const std::string addr = "file://" + path;
	std::filesystem::remove(path);

	// io is not core: with no provider loaded, the facade can't resolve an
	// IIoService, so open() fails up front.
	if (auto before = thx::io::open(addr, thx::io::Mode::Read); !before)
		std::printf("before load: open() -> %s\n", before.error().message.c_str());

	// Discover every plugin directory passed on the command line. The in-tree io
	// plugins build into per-component dirs; an install co-locates them, so a
	// single dir is the common case.
	for (int i = 1; i < argc; ++i)
	{
		if (auto r = thx::plugin::discover(argv[i]); !r)
		{
			std::fprintf(stderr, "discover(%s) failed: %s\n", argv[i], r.error().message.c_str());
			thx::shutdown();
			return 1;
		}
	}

	// The dispatcher is a discoverable provider — no DSO is mapped to list it.
	auto providers = thx::plugin::pluginsProviding<thx::io::IIoService>();
	std::printf("io providers (%zu):\n", providers.size());
	for (auto const& p : providers)
		std::printf("  %s @ %u.%u.%u\n", p.name.c_str(), p.version.major, p.version.minor, p.version.patch);
	if (providers.empty())
	{
		std::fprintf(stderr, "no IIoService provider discovered\n");
		thx::shutdown();
		return 1;
	}

	// Load the file:// handler *with its dependencies* — it requires the
	// IIoService provider, so loadWithDependencies pulls the provider in and
	// loads it first. (This targets just the io plugins; it doesn't load
	// whatever else happens to share the directory.)
	auto fileHandler = thx::plugin::pluginByName("thx.io.FileProtocol");
	if (!fileHandler)
	{
		std::fprintf(stderr, "no file:// handler (thx.io.FileProtocol) discovered\n");
		thx::shutdown();
		return 1;
	}
	auto summary = thx::plugin::loadWithDependencies(fileHandler->path);
	std::printf("loaded %zu plugin(s) (file handler + its IIoService provider)\n",
				summary.loaded.size());
	if (!summary.failed.empty())
	{
		for (auto const& [p, err] : summary.failed)
			std::fprintf(stderr, "load failed: %s: %s\n", p.c_str(), err.message.c_str());
		thx::shutdown();
		return 1;
	}

	int rc = 0;

	// Write through the facade — routed to the file:// handler plugin.
	{
		const std::string msg = "hello from the thorax io subsystem";
		auto w = thx::io::open(addr, thx::io::Mode::Write);
		if (!w)
		{
			std::fprintf(stderr, "open(write) failed: %s\n", w.error().message.c_str());
			thx::shutdown();
			return 1;
		}
		w.value()->write(bytesOf(msg));
		std::printf("wrote %zu bytes to %s\n", msg.size(), addr.c_str());
	} // drop the StreamHandle before unloading any plugin

	// Read it back.
	{
		auto r = thx::io::open(addr, thx::io::Mode::Read);
		if (!r)
		{
			std::fprintf(stderr, "open(read) failed: %s\n", r.error().message.c_str());
			thx::shutdown();
			return 1;
		}
		std::uint8_t buf[128] = {};
		auto n = r.value()->read({buf, sizeof(buf)});
		if (n < 0)
		{
			std::fprintf(stderr, "read failed\n");
			rc = 1;
		}
		else
		{
			std::printf("read back: %.*s\n", static_cast<int>(n), reinterpret_cast<char*>(buf));
		}
	} // drop the StreamHandle before shutdown unloads the plugins

	std::filesystem::remove(path);
	thx::shutdown();
	return rc;
}
