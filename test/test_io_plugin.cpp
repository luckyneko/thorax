/*
 *  Created by LuckyNeko on 25/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/plugin/plugin_manager.h>
#include <thx/service/service_manager.h>
#include <thx/plugins/io/io_service.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#ifndef THX_IO_PLUGIN_PATH
#  error "THX_IO_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

using namespace thx::plugins::io;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

struct Fixture
{
	thx::service::ServiceManager sm;
	thx::plugin::PluginManager   loader{sm};

	explicit Fixture() { REQUIRE(loader.load(THX_IO_PLUGIN_PATH)); }

	std::shared_ptr<IIOService> service()
	{
		return sm.getService<IIOService>();
	}
};

// Write content to a temp file; return the path.
std::string makeTempFile(const char* name, const char* content)
{
	namespace fs = std::filesystem;
	auto path = (fs::temp_directory_path() / name).string();
	std::ofstream{path} << content;
	return path;
}

// Custom reader used in provider-dispatch tests.
// Accepts only files whose path ends with the given suffix.
struct SuffixReader : IFileReader
{
	std::string m_suffix;
	int         m_readCount{0};

	explicit SuffixReader(std::string suffix) : m_suffix(std::move(suffix)) {}

	bool canRead(const char* path) override
	{
		if (!path)
			return false;
		std::string p(path);
		return p.size() >= m_suffix.size() &&
		       p.compare(p.size() - m_suffix.size(), m_suffix.size(), m_suffix) == 0;
	}

	int read(const char* path, char* buffer, int buffer_size) override
	{
		if (!path || !buffer || buffer_size <= 0)
			return -1;
		++m_readCount;
		// Prepend a marker so tests can verify this reader was called.
		const char* marker = "[suffix]";
		int mlen = static_cast<int>(std::strlen(marker));
		if (buffer_size <= mlen)
			return -1;
		std::memcpy(buffer, marker, static_cast<std::size_t>(mlen));

		std::ifstream f(path, std::ios::binary);
		if (!f)
			return mlen;
		f.read(buffer + mlen, static_cast<std::streamsize>(buffer_size - mlen - 1));
		return mlen + static_cast<int>(f.gcount());
	}
};

} // namespace

// ---------------------------------------------------------------------------
// Load & registration
// ---------------------------------------------------------------------------

TEST_CASE("IOPlugin - loads and registers IIOService", "[io_plugin]")
{
	Fixture f;
	REQUIRE(f.service() != nullptr);
}

// ---------------------------------------------------------------------------
// No readers → read fails
// ---------------------------------------------------------------------------

TEST_CASE("IOPlugin - read with no readers returns -1", "[io_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	auto path = makeTempFile("thx_io_nordr.txt", "hello");
	char buf[64]{};
	REQUIRE(svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf))) == -1);
	std::filesystem::remove(path);
}

// ---------------------------------------------------------------------------
// makeTextReader
// ---------------------------------------------------------------------------

TEST_CASE("IOPlugin - makeTextReader returns non-null", "[io_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);
	REQUIRE(svc->makeTextReader() != nullptr);
}

TEST_CASE("IOPlugin - text reader reads file content", "[io_plugin]")
{
	auto path = makeTempFile("thx_io_txt.txt", "hello world");

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	auto txt = svc->makeTextReader();
	svc->addReader(txt);

	char buf[64]{};
	int  n = svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf)));

	std::filesystem::remove(path);

	REQUIRE(n > 0);
	REQUIRE(std::string(buf, static_cast<std::size_t>(n)) == "hello world");
}

// ---------------------------------------------------------------------------
// addReader / removeReader
// ---------------------------------------------------------------------------

TEST_CASE("IOPlugin - removeReader stops delivery", "[io_plugin]")
{
	auto path = makeTempFile("thx_io_rm.txt", "data");

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	auto txt = svc->makeTextReader();
	svc->addReader(txt);
	REQUIRE(svc->read(path.c_str(), nullptr, 0) == -2); // reader accepted but read failed

	svc->removeReader(txt.get());

	char buf[64]{};
	REQUIRE(svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf))) == -1);

	std::filesystem::remove(path);
}

TEST_CASE("IOPlugin - expired reader is culled automatically", "[io_plugin]")
{
	auto path = makeTempFile("thx_io_exp.txt", "data");

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	{
		auto txt = svc->makeTextReader();
		svc->addReader(txt);
	} // txt released — weak_ptr expires

	char buf[64]{};
	// Should not crash; returns -1 because no live readers remain.
	REQUIRE_NOTHROW(svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf))));

	std::filesystem::remove(path);
}

TEST_CASE("IOPlugin - null reader is ignored", "[io_plugin]")
{
	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	REQUIRE_NOTHROW(svc->addReader(nullptr));

	char buf[64]{};
	REQUIRE(svc->read("any.txt", buf, static_cast<int>(sizeof(buf))) == -1);
}

// ---------------------------------------------------------------------------
// Provider dispatch
//
// Demonstrates M6 composable pattern: a specific reader registered before the
// generic TextReader wins for matching paths; TextReader handles the rest.
// ---------------------------------------------------------------------------

TEST_CASE("IOPlugin - specific reader takes priority over text reader", "[io_plugin]")
{
	namespace fs = std::filesystem;

	auto json_path = makeTempFile("thx_io_dispatch.json", "{}");
	auto txt_path  = makeTempFile("thx_io_dispatch.txt",  "plain");

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	// Register specific reader first, then the generic fallback.
	// Both shared_ptrs must be kept alive; the service only holds weak_ptrs.
	auto json_reader = std::make_shared<SuffixReader>(".json");
	auto txt_reader  = svc->makeTextReader();
	svc->addReader(json_reader);
	svc->addReader(txt_reader);

	char buf[128]{};

	// .json file → SuffixReader (marker prefix present)
	int n_json = svc->read(json_path.c_str(), buf, static_cast<int>(sizeof(buf)));
	REQUIRE(n_json > 0);
	REQUIRE(std::string(buf, 8) == "[suffix]"); // marker written by SuffixReader
	REQUIRE(json_reader->m_readCount == 1);

	// .txt file → TextReader (no marker)
	std::memset(buf, 0, sizeof(buf));
	int n_txt = svc->read(txt_path.c_str(), buf, static_cast<int>(sizeof(buf)));
	REQUIRE(n_txt > 0);
	REQUIRE(std::string(buf, static_cast<std::size_t>(n_txt)) == "plain");
	REQUIRE(json_reader->m_readCount == 1); // json reader not called for .txt

	fs::remove(json_path);
	fs::remove(txt_path);
}

TEST_CASE("IOPlugin - unregistered extension falls through to text reader",
          "[io_plugin]")
{
	auto path = makeTempFile("thx_io_fallthru.bin", "binary");

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	// Only add a .json-specific reader + generic fallback.
	// Both shared_ptrs must be kept alive; the service only holds weak_ptrs.
	auto json_reader = std::make_shared<SuffixReader>(".json");
	auto txt_reader  = svc->makeTextReader();
	svc->addReader(json_reader);
	svc->addReader(txt_reader);

	char buf[64]{};
	int n = svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf)));
	REQUIRE(n > 0);
	REQUIRE(std::string(buf, static_cast<std::size_t>(n)) == "binary");

	std::filesystem::remove(path);
}

TEST_CASE("IOPlugin - -1 means no reader, -2 means reader accepted but failed",
          "[io_plugin]")
{
	auto path = makeTempFile("thx_io_errcode.txt", "data");

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	char buf[64]{};

	// No reader registered → -1
	REQUIRE(svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf))) == -1);

	auto txt = svc->makeTextReader();
	svc->addReader(txt);

	// Reader registered, null buffer forces the reader's internal check to fail → -2
	REQUIRE(svc->read(path.c_str(), nullptr, 0) == -2);

	// Valid read succeeds (≥ 0)
	REQUIRE(svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf))) >= 0);

	std::filesystem::remove(path);
}

TEST_CASE("IOPlugin - reader removed at plugin scope still evicted correctly",
          "[io_plugin]")
{
	auto path = makeTempFile("thx_io_scope.txt", "data");

	Fixture f;
	auto svc = f.service();
	REQUIRE(svc);

	// Simulate a plugin registering, then being unloaded (shared_ptr dropped).
	{
		auto reader = std::make_shared<SuffixReader>(".txt");
		svc->addReader(reader);
		char buf[64]{};
		int n = svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf)));
		REQUIRE(n > 0);
	} // reader dropped → weak_ptr expires

	// Next read with the same service should not crash.
	char buf[64]{};
	REQUIRE_NOTHROW(svc->read(path.c_str(), buf, static_cast<int>(sizeof(buf))));

	std::filesystem::remove(path);
}
