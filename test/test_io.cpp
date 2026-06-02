/*
 *  Created by LuckyNeko on 01/06/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/io/io.h>
#include <thx/io/stream.h>
#include <thx/plugin/plugin.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>

// io tests — io is not part of core, so these load the io provider plugin
// (IIoService) plus the file:// handler plugin, then exercise file streaming
// through the thx::io facade. The per-case reset listener (thx::shutdown())
// unloads both plugins and unregisters the IIoService between cases.

#ifndef THX_IO_SERVICE_PLUGIN_PATH
#	error "THX_IO_SERVICE_PLUGIN_PATH not defined — set via target_compile_definitions"
#endif
#ifndef THX_IO_FILE_PLUGIN_PATH
#	error "THX_IO_FILE_PLUGIN_PATH not defined — set via target_compile_definitions"
#endif

namespace
{
	// Load the IIoService provider then the file:// handler (which requires it).
	void loadFileIo()
	{
		REQUIRE(thx::plugin::load(THX_IO_SERVICE_PLUGIN_PATH));
		REQUIRE(thx::plugin::load(THX_IO_FILE_PLUGIN_PATH));
	}

	std::string tmpPath(const char* name)
	{
		return (std::filesystem::temp_directory_path() / name).string();
	}

	thx::Span<const std::uint8_t> bytesOf(const char* s)
	{
		return {reinterpret_cast<const std::uint8_t*>(s), std::strlen(s)};
	}
} // namespace

TEST_CASE("io - file:// write then read round-trip", "[io]")
{
	loadFileIo();
	auto path = tmpPath("thx_io_rt.bin");
	std::filesystem::remove(path);
	const std::string addr = "file://" + path;
	const char* msg = "hello thorax io";

	{
		auto opened = thx::io::open(addr, thx::io::Mode::Write);
		REQUIRE(opened);
		auto& s = opened.value();
		REQUIRE(s);
		REQUIRE(s->canWrite());
		REQUIRE(s->write(bytesOf(msg)) == static_cast<std::int64_t>(std::strlen(msg)));
	} // stream closes here

	{
		auto opened = thx::io::open(addr, thx::io::Mode::Read);
		REQUIRE(opened);
		auto& s = opened.value();
		std::uint8_t buf[64] = {};
		auto n = s->read({buf, sizeof(buf)});
		REQUIRE(n == static_cast<std::int64_t>(std::strlen(msg)));
		REQUIRE(std::string(reinterpret_cast<char*>(buf), static_cast<std::size_t>(n)) == msg);
		REQUIRE(s->read({buf, sizeof(buf)}) == 0); // at EOF
	}

	std::filesystem::remove(path);
}

TEST_CASE("io - a bare path defaults to the file handler", "[io]")
{
	loadFileIo();
	auto path = tmpPath("thx_io_bare.bin");
	std::filesystem::remove(path);

	{
		auto opened = thx::io::open(path, thx::io::Mode::Write); // no scheme
		REQUIRE(opened);
		REQUIRE(opened.value()->write(bytesOf("bare")) == 4);
	}
	{
		auto opened = thx::io::open(path, thx::io::Mode::Read);
		REQUIRE(opened);
		std::uint8_t buf[8] = {};
		REQUIRE(opened.value()->read({buf, sizeof(buf)}) == 4);
		REQUIRE(std::memcmp(buf, "bare", 4) == 0);
	}

	std::filesystem::remove(path);
}

TEST_CASE("io - unknown scheme yields NoHandler", "[io]")
{
	loadFileIo();
	auto opened = thx::io::open("ftp://example.com/x", thx::io::Mode::Read);
	REQUIRE_FALSE(opened);
	REQUIRE(opened.error().code == thx::ErrorCode::NoHandler);
}

TEST_CASE("io - open with no provider loaded yields NoHandler", "[io]")
{
	// No plugin loaded: the facade can't resolve an IIoService.
	auto opened = thx::io::open("file:///tmp/whatever", thx::io::Mode::Read);
	REQUIRE_FALSE(opened);
	REQUIRE(opened.error().code == thx::ErrorCode::NoHandler);
}

TEST_CASE("io - opening a missing file fails", "[io]")
{
	loadFileIo();
	auto opened = thx::io::open("file://" + tmpPath("thx_io_does_not_exist.bin"),
								thx::io::Mode::Read);
	REQUIRE_FALSE(opened);
	REQUIRE(opened.error().code == thx::ErrorCode::OpenFailed);
}

TEST_CASE("io - writing a read-only stream fails", "[io]")
{
	loadFileIo();
	auto path = tmpPath("thx_io_ro.bin");
	std::filesystem::remove(path);
	{
		auto w = thx::io::open("file://" + path, thx::io::Mode::Write);
		REQUIRE(w);
		REQUIRE(w.value()->write(bytesOf("data")) == 4);
	}

	{
		auto opened = thx::io::open("file://" + path, thx::io::Mode::Read);
		REQUIRE(opened);
		auto& s = opened.value();
		REQUIRE_FALSE(s->canWrite());
		std::uint8_t b = 'x';
		REQUIRE(s->write({&b, 1}) < 0);
	} // stream closes here

	std::filesystem::remove(path);
}

TEST_CASE("io - seek and tell on a file stream", "[io]")
{
	loadFileIo();
	auto path = tmpPath("thx_io_seek.bin");
	std::filesystem::remove(path);
	{
		auto w = thx::io::open("file://" + path, thx::io::Mode::Write);
		REQUIRE(w);
		REQUIRE(w.value()->write(bytesOf("0123456789")) == 10);
	}

	{
		auto opened = thx::io::open("file://" + path, thx::io::Mode::Read);
		REQUIRE(opened);
		auto& s = opened.value();
		REQUIRE(s->canSeek());
		REQUIRE(s->seek(5, thx::io::Whence::Begin) == 5);
		REQUIRE(s->tell() == 5);

		std::uint8_t buf[4] = {};
		REQUIRE(s->read({buf, sizeof(buf)}) == 4);
		REQUIRE(std::memcmp(buf, "5678", 4) == 0);
	} // stream closes here

	std::filesystem::remove(path);
}
