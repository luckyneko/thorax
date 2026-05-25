/*
 *  Created by LuckyNeko on 25/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/plugin/manifest.h>

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>

#ifndef THX_EMIT_MANIFEST_PATH
#  error "THX_EMIT_MANIFEST_PATH not defined — set via target_compile_definitions"
#endif

#ifndef THX_MOCK_PLUGIN_PATH
#  error "THX_MOCK_PLUGIN_PATH not defined"
#endif

#ifndef THX_MOCK_MULTI_PLUGIN_PATH
#  error "THX_MOCK_MULTI_PLUGIN_PATH not defined"
#endif

namespace
{
	int runEmit(std::string const& dso, std::string const& outPath)
	{
		// Quote both arguments so paths containing spaces survive system().
		std::ostringstream cmd;
		cmd << '"' << THX_EMIT_MANIFEST_PATH << '"'
		    << ' ' << '"' << dso << '"'
		    << ' ' << '"' << outPath << '"';
		return std::system(cmd.str().c_str());
	}

	std::string tempOut(std::string const& tag)
	{
		auto p = std::filesystem::temp_directory_path()
		    / ("thx_test_emit_" + tag + ".thx.json");
		std::filesystem::remove(p);
		return p.string();
	}
}

TEST_CASE("thx_emit_manifest - extracts a single-service plugin manifest",
          "[emit_manifest][integration]")
{
	auto outPath = tempOut("mock");

	REQUIRE(runEmit(THX_MOCK_PLUGIN_PATH, outPath) == 0);

	auto parsed = thx::plugin::parseManifest(outPath);
	REQUIRE(parsed);
	auto const& m = parsed.value();

	REQUIRE(m.schema  == 1);
	REQUIRE(m.name    == "thx_mock.MockService");
	REQUIRE(m.version == thx::Version{1, 0, 0});
	REQUIRE(m.provides.size() == 1);
	REQUIRE(m.provides[0] == "thx_mock.MockService");
	REQUIRE(m.requirements.empty());

	std::filesystem::remove(outPath);
}

TEST_CASE("thx_emit_manifest - extracts a multi-service plugin with requirements",
          "[emit_manifest][integration]")
{
	auto outPath = tempOut("multi");

	REQUIRE(runEmit(THX_MOCK_MULTI_PLUGIN_PATH, outPath) == 0);

	auto parsed = thx::plugin::parseManifest(outPath);
	REQUIRE(parsed);
	auto const& m = parsed.value();

	REQUIRE(m.name == "thx.mock.MultiPlugin");
	REQUIRE(m.version == thx::Version{1, 0, 0});

	REQUIRE(m.provides.size() == 2);
	// Order matches the IPlugin::provides() return order — tool doesn't sort.
	REQUIRE(m.provides[0] == "thx_mock.ServiceA");
	REQUIRE(m.provides[1] == "thx_mock.ServiceB");

	REQUIRE(m.requirements.size() == 1);
	REQUIRE(m.requirements[0].id == "thx_mock.MockService");
	REQUIRE(m.requirements[0].version == thx::Version{1, 0, 0});

	std::filesystem::remove(outPath);
}

TEST_CASE("thx_emit_manifest - returns non-zero on a missing DSO",
          "[emit_manifest][integration]")
{
	auto outPath = tempOut("missing");
	int rc = runEmit("/nonexistent/path/to.dylib", outPath);
	REQUIRE(rc != 0);

	// The output file should not have been created.
	REQUIRE_FALSE(std::filesystem::exists(outPath));
}
