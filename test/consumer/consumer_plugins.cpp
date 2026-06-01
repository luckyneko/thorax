/*
 *  Smoke-test that plugin headers are correctly installed and usable, and
 *  (if argv[1] points at a plugins install dir) that discover() finds the
 *  installed sidecar manifests + their paired DSOs.
 *
 *  Compiled only when THX_LOGGING_INCLUDE and THX_IO_INCLUDE are found.
 */

#include <thx/log/log_service.h>
#include <thx/plugin/plugin.h>
#include <thx/plugins/io/io_service.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char* argv[])
{
	// Verify the service IDs are reachable as constexpr values.
	constexpr auto logging_id = thx::log::ILogService::staticId();
	constexpr auto io_id = thx::plugins::io::IIOService::staticId();

	std::printf("logging service id: %s\n", logging_id.name());
	std::printf("io service id:      %s\n", io_id.name());

	if (argc < 2)
		return 0; // header-only smoke; runtime discover is optional.

	std::string const plugins_dir = argv[1];
	auto disc = thx::plugin::discover(plugins_dir);
	if (!disc)
	{
		std::fprintf(stderr, "discover(%s) failed: %s\n",
					 plugins_dir.c_str(), disc.error().message.c_str());
		return 1;
	}

	auto found = thx::plugin::plugins(thx::plugin::State::Discovered);
	std::printf("discovered %zu plugin(s) in %s\n", found.size(), plugins_dir.c_str());
	for (auto const& info : found)
		std::printf("  %s @ %u.%u.%u  (%s)\n",
					info.name.c_str(),
					info.version.major, info.version.minor, info.version.patch,
					info.path.c_str());

	// We expect to find at least the two in-tree plugins (logging + io).
	std::vector<std::string> names;
	names.reserve(found.size());
	for (auto const& info : found)
		names.push_back(info.name);
	auto has = [&](char const* n)
	{
		return std::find(names.begin(), names.end(), n) != names.end();
	};
	if (!has("thx.spdlog.SpdlogService") || !has("thx.plugins.io.IIOService"))
	{
		std::fprintf(stderr,
					 "discover did not find both expected plugins (logging + io)\n");
		return 2;
	}

	return 0;
}
