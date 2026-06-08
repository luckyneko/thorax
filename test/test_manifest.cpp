/*
 *  Created by LuckyNeko on 23/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/plugin/manifest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace
{
	// Helper: write `contents` to a temp file and return the path.
	// The path is unique per test by appending a counter; cleanup is left
	// to the test (or the OS) so test failures don't lose the artefact.
	std::string writeTempManifest(const std::string& tag, const std::string& contents)
	{
		namespace fs = std::filesystem;
		static int counter = 0;
		auto p = fs::temp_directory_path() / ("thx_test_manifest_" + tag + "_" + std::to_string(++counter) + ".thx.json");
		std::ofstream f(p);
		f << contents;
		return p.string();
	}
} // namespace

TEST_CASE("parseManifest - well-formed minimal manifest parses cleanly",
		  "[manifest]")
{
	auto path = writeTempManifest("minimal", R"({
		"schema":   1,
		"name":     "thx.test.MinimalPlugin",
		"version":  "1.2.3",
		"provides": [],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE(r);
	REQUIRE(r.value().schema == 1);
	REQUIRE(r.value().name == "thx.test.MinimalPlugin");
	REQUIRE(r.value().version == thx::Version{1, 2, 3});
	REQUIRE(r.value().provides.empty());
	REQUIRE(r.value().requirements.empty());
}

TEST_CASE("parseManifest - populated provides and requires", "[manifest]")
{
	auto path = writeTempManifest("populated", R"({
		"schema":   1,
		"name":     "thx.cameras.AcmeCameraDriver",
		"version":  "1.2.0",
		"provides": ["thx.cameras.ICameraDriver", "thx.bus.IUsbDevice"],
		"requires": [
			{"id": "thx.io.ILogService",   "version": "1.0.0"},
			{"id": "thx.gpu.IShaderCache", "version": "2.5.1"}
		]
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE(r);
	const auto& m = r.value();

	REQUIRE(m.provides.size() == 2);
	REQUIRE(m.provides[0] == "thx.cameras.ICameraDriver");
	REQUIRE(m.provides[1] == "thx.bus.IUsbDevice");

	REQUIRE(m.requirements.size() == 2);
	REQUIRE(m.requirements[0].id == "thx.io.ILogService");
	REQUIRE(m.requirements[0].version == thx::Version{1, 0, 0});
	REQUIRE(m.requirements[1].id == "thx.gpu.IShaderCache");
	REQUIRE(m.requirements[1].version == thx::Version{2, 5, 1});
}

TEST_CASE("parseManifest - tolerates field ordering", "[manifest]")
{
	auto path = writeTempManifest("reordered", R"({
		"requires": [],
		"version":  "0.0.1",
		"provides": ["thx.X"],
		"name":     "thx.X",
		"schema":   1
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE(r);
	REQUIRE(r.value().name == "thx.X");
	REQUIRE(r.value().version == thx::Version{0, 0, 1});
}

TEST_CASE("parseManifest - unknown fields are ignored", "[manifest]")
{
	auto path = writeTempManifest("extra", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [],
		"requires": [],
		"future_field":   "ignored",
		"another_extra":  {"nested": [1, 2, 3]},
		"and_an_array":   ["a", "b"]
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE(r);
	REQUIRE(r.value().name == "thx.test.X");
}

TEST_CASE("parseManifest - missing file returns FileNotFound", "[manifest]")
{
	auto r = thx::plugin::parseManifest("/nonexistent/path/missing.thx.json");
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("parseManifest - missing schema field fails", "[manifest]")
{
	auto path = writeTempManifest("no_schema", R"({
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
	REQUIRE(r.error().message.find("schema") != std::string::npos);
}

TEST_CASE("parseManifest - unsupported schema version fails", "[manifest]")
{
	auto path = writeTempManifest("schema2", R"({
		"schema":   2,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
	REQUIRE(r.error().message.find("schema") != std::string::npos);
	REQUIRE(r.error().message.find("2") != std::string::npos);
}

TEST_CASE("parseManifest - missing name fails", "[manifest]")
{
	auto path = writeTempManifest("no_name", R"({
		"schema":   1,
		"version":  "1.0.0",
		"provides": [],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
	REQUIRE(r.error().message.find("name") != std::string::npos);
}

TEST_CASE("parseManifest - missing provides fails", "[manifest]")
{
	auto path = writeTempManifest("no_provides", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
	REQUIRE(r.error().message.find("provides") != std::string::npos);
}

TEST_CASE("parseManifest - missing requires fails", "[manifest]")
{
	auto path = writeTempManifest("no_requires", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
	REQUIRE(r.error().message.find("requires") != std::string::npos);
}

TEST_CASE("parseManifest - malformed JSON (unclosed brace) fails", "[manifest]")
{
	auto path = writeTempManifest("unclosed", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [],
		"requires": []
	)");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
}

TEST_CASE("parseManifest - malformed JSON (missing colon) fails", "[manifest]")
{
	auto path = writeTempManifest("no_colon", R"({
		"schema"   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
}

TEST_CASE("parseManifest - malformed version string fails", "[manifest]")
{
	auto path = writeTempManifest("bad_version", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.2",
		"provides": [],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
	REQUIRE(r.error().message.find("version") != std::string::npos);
}

TEST_CASE("parseManifest - requires entry missing version fails", "[manifest]")
{
	auto path = writeTempManifest("partial_req", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [],
		"requires": [{"id": "thx.io.ILogService"}]
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
}

TEST_CASE("parseManifest - provides with non-string entry fails", "[manifest]")
{
	auto path = writeTempManifest("provides_int", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [42],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
}

TEST_CASE("parseManifest - escape sequences in strings work", "[manifest]")
{
	// JSON requires \\ for a literal backslash. The raw string literal hides
	// the C++ escaping so the JSON payload reads cleanly.
	auto path = writeTempManifest("escapes", R"({
		"schema":   1,
		"name":     "thx.test.with\"quote",
		"version":  "1.0.0",
		"provides": ["thx.path.with\\slash"],
		"requires": []
	})");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE(r);
	REQUIRE(r.value().name == "thx.test.with\"quote");
	REQUIRE(r.value().provides.size() == 1);
	REQUIRE(r.value().provides[0] == "thx.path.with\\slash");
}

TEST_CASE("parseManifest - trailing garbage after object fails", "[manifest]")
{
	auto path = writeTempManifest("trailing", R"({
		"schema":   1,
		"name":     "thx.test.X",
		"version":  "1.0.0",
		"provides": [],
		"requires": []
	} extra_stuff)");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
}

TEST_CASE("parseManifest - empty file fails", "[manifest]")
{
	auto path = writeTempManifest("empty", "");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
}

// ---------------------------------------------------------------------------
// serialiseManifest
// ---------------------------------------------------------------------------

namespace
{
	// Write `contents` to a temp file and return the path. Used to round-trip
	// serialised output through parseManifest.
	std::string writeTempJson(const std::string& tag, const std::string& contents)
	{
		namespace fs = std::filesystem;
		static int counter = 0;
		auto p = fs::temp_directory_path() / ("thx_test_serialise_" + tag + "_" + std::to_string(++counter) + ".thx.json");
		std::ofstream f(p);
		f << contents;
		return p.string();
	}
} // namespace

TEST_CASE("serialiseManifest - empty provides and requires", "[manifest][serialise]")
{
	thx::plugin::PluginManifest m;
	m.schema = 1;
	m.name = "thx.test.Empty";
	m.version = thx::Version{1, 2, 3};
	auto json = thx::plugin::serialiseManifest(m);

	REQUIRE(json.find("\"schema\":   1") != std::string::npos);
	REQUIRE(json.find("\"name\":     \"thx.test.Empty\"") != std::string::npos);
	REQUIRE(json.find("\"version\":  \"1.2.3\"") != std::string::npos);
	REQUIRE(json.find("\"provides\": []") != std::string::npos);
	REQUIRE(json.find("\"requires\": []") != std::string::npos);
	REQUIRE(json.back() == '\n');
}

TEST_CASE("serialiseManifest - round-trip preserves all fields", "[manifest][serialise]")
{
	thx::plugin::PluginManifest original;
	original.schema = 1;
	original.name = "thx.cameras.AcmeCameraDriver";
	original.version = thx::Version{1, 2, 0};
	original.provides = {"thx.cameras.ICameraDriver", "thx.bus.IUsbDevice"};
	original.requirements = {
		{"thx.io.ILogService", thx::Version{1, 0, 0}},
		{"thx.gpu.IShaderCache", thx::Version{2, 5, 1}},
	};

	auto json = thx::plugin::serialiseManifest(original);
	auto path = writeTempJson("roundtrip", json);

	auto parsed = thx::plugin::parseManifest(path);
	REQUIRE(parsed);
	const auto& r = parsed.value();

	REQUIRE(r.schema == original.schema);
	REQUIRE(r.name == original.name);
	REQUIRE(r.version == original.version);
	REQUIRE(r.provides == original.provides);
	REQUIRE(r.requirements.size() == original.requirements.size());
	for (std::size_t i = 0; i < r.requirements.size(); ++i)
	{
		REQUIRE(r.requirements[i].id == original.requirements[i].id);
		REQUIRE(r.requirements[i].version == original.requirements[i].version);
	}
}

TEST_CASE("serialiseManifest - escapes special characters in strings",
		  "[manifest][serialise]")
{
	thx::plugin::PluginManifest m;
	m.schema = 1;
	m.name = "thx.test.with\"quote";
	m.version = thx::Version{1, 0, 0};
	m.provides = {"thx.path.with\\slash"};

	auto json = thx::plugin::serialiseManifest(m);
	auto path = writeTempJson("escapes", json);

	auto parsed = thx::plugin::parseManifest(path);
	REQUIRE(parsed);
	REQUIRE(parsed.value().name == "thx.test.with\"quote");
	REQUIRE(parsed.value().provides.size() == 1);
	REQUIRE(parsed.value().provides[0] == "thx.path.with\\slash");
}

// ---------------------------------------------------------------------------
// Recursion depth limit
// ---------------------------------------------------------------------------

TEST_CASE("parseManifest - deeply-nested unknown field is rejected", "[manifest]")
{
	// Build a manifest whose `extra` field is a deeply-nested JSON array.
	// 64 levels is well above the documented kSkipValueMaxDepth (32), so the
	// parser should bail with MalformedManifest rather than stack-overflowing.
	std::string nested = "[]";
	for (int i = 0; i < 64; ++i)
		nested = "[" + nested + "]";

	auto path = writeTempManifest("deep_nest",
								  "{\n"
								  "\t\"schema\":   1,\n"
								  "\t\"name\":     \"thx.test.Deep\",\n"
								  "\t\"version\":  \"1.0.0\",\n"
								  "\t\"provides\": [],\n"
								  "\t\"requires\": [],\n"
								  "\t\"extra\":    " +
									  nested + "\n"
											   "}");

	auto r = thx::plugin::parseManifest(path);
	REQUIRE_FALSE(r);
	REQUIRE(r.error().code == thx::ErrorCode::MalformedManifest);
}
