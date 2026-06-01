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

#include <httplib.h>

#include <cstdint>
#include <string>
#include <thread>

#ifndef THX_HTTP_PLUGIN_PATH
#	error "THX_HTTP_PLUGIN_PATH not defined — set via target_compile_definitions in CMakeLists.txt"
#endif

// The http plugin contributes an http:// IProtocol to the core thx::io service.
// We run a cpp-httplib server on a loopback port (fully in-process, no external
// network), load the plugin, and read the served payload back through
// thx::io::open. The per-case reset listener unloads the plugin afterwards; the
// StreamHandle is dropped before that so no stream dtor runs in an unmapped DSO.

TEST_CASE("HttpPlugin - reads an http:// resource through thx::io", "[http_plugin]")
{
	const std::string payload = "thorax http payload — 0123456789 abcdefghij";

	httplib::Server server;
	server.Get("/data", [&](httplib::Request const&, httplib::Response& res)
			   { res.set_content(payload, "application/octet-stream"); });

	int port = server.bind_to_any_port("127.0.0.1");
	REQUIRE(port > 0);
	std::thread serverThread([&]
							 { server.listen_after_bind(); });
	while (!server.is_running())
		std::this_thread::yield();

	REQUIRE(thx::plugin::load(THX_HTTP_PLUGIN_PATH));

	{
		const std::string url = "http://127.0.0.1:" + std::to_string(port) + "/data";
		auto opened = thx::io::open(url, thx::io::Mode::Read);
		REQUIRE(opened);
		auto& stream = opened.value();
		REQUIRE(stream->canRead());
		REQUIRE_FALSE(stream->canWrite());

		std::string got;
		std::uint8_t buf[16];
		for (;;)
		{
			auto n = stream->read({buf, sizeof(buf)});
			REQUIRE(n >= 0);
			if (n == 0)
				break;
			got.append(reinterpret_cast<char*>(buf), static_cast<std::size_t>(n));
		}
		REQUIRE(got == payload);
	} // drop the StreamHandle before the plugin is unloaded

	server.stop();
	serverThread.join();
}

TEST_CASE("HttpPlugin - write mode is unsupported", "[http_plugin]")
{
	REQUIRE(thx::plugin::load(THX_HTTP_PLUGIN_PATH));
	auto opened = thx::io::open("http://127.0.0.1:1/x", thx::io::Mode::Write);
	REQUIRE_FALSE(opened);
	REQUIRE(opened.error().code == thx::ErrorCode::Unsupported);
}
