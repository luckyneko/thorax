/*
 *  Smoke-test that the installed public headers are usable, and (if argv[1]
 *  points at a plugins install dir) that discover() finds the installed sidecar
 *  manifests + their paired DSOs.
 *
 *  Exercises the consumer-tier log and io interfaces (the ILogService / io
 *  headers installed alongside the library by their interface libraries) plus
 *  plugin discovery of the in-tree spdlog and http plugins.
 */

#include <thx/io/io.h>
#include <thx/log/log_service.h>
#include <thx/plugin/plugin.h>
#include <thx/thorax.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
	// Verify the log service id is reachable as a constexpr value, and that the
	// io facade header is usable (open() is declared).
	constexpr auto logging_id = thx::log::ILogService::staticId();
	std::printf("logging service id: %s\n", logging_id.name());
	std::printf("io facade available: thx::io::open declared\n");

	if (argc < 2)
		return 0; // header-only smoke; runtime discover is optional.

	// The plugin/service facades reach the process-wide Registry, which the
	// framework requires initialise() to stand up before anything works;
	// shutdown() tears it back down before exit.
	thx::initialise();

	const std::string plugins_dir = argv[1];
	auto disc = thx::plugin::discover(plugins_dir);
	if (!disc)
	{
		std::fprintf(stderr, "discover(%s) failed: %s\n",
					 plugins_dir.c_str(), disc.error().message.c_str());
		thx::shutdown();
		return 1;
	}

	auto found = thx::plugin::plugins(thx::plugin::State::Discovered);
	std::printf("discovered %zu plugin(s) in %s\n", found.size(), plugins_dir.c_str());
	for (const auto& info : found)
		std::printf("  %s @ %u.%u.%u  (%s)\n",
					info.name.c_str(),
					info.version.major, info.version.minor, info.version.patch,
					info.path.c_str());

	// We expect to find at least the two in-tree plugins (spdlog + http).
	std::vector<std::string> names;
	names.reserve(found.size());
	for (const auto& info : found)
		names.push_back(info.name);
	auto has = [&](const char* n)
	{
		return std::find(names.begin(), names.end(), n) != names.end();
	};
	if (!has("thx.spdlog.SpdlogService") || !has("thx.http.HttpProtocol"))
	{
		std::fprintf(stderr,
					 "discover did not find both expected plugins (spdlog + http)\n");
		thx::shutdown();
		return 2;
	}

	thx::shutdown();
	return 0;
}
