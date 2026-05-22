/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/service/service_id.h>

#include <unordered_map>

TEST_CASE("ServiceID - same name compares equal", "[service_id]")
{
	constexpr thx::service::ServiceID a("thx.io.FileService");
	constexpr thx::service::ServiceID b("thx.io.FileService");

	STATIC_REQUIRE(a == b);
	STATIC_REQUIRE(!(a != b));
}

TEST_CASE("ServiceID - different names are not equal", "[service_id]")
{
	constexpr thx::service::ServiceID a("thx.io.FileService");
	constexpr thx::service::ServiceID b("thx.io.AudioService");

	STATIC_REQUIRE(a != b);
	STATIC_REQUIRE(!(a == b));
}

TEST_CASE("ServiceID - hash is consistent for the same name", "[service_id]")
{
	constexpr thx::service::ServiceID a("thx.test.MyService");
	constexpr thx::service::ServiceID b("thx.test.MyService");

	STATIC_REQUIRE(a.hash() == b.hash());
}

TEST_CASE("ServiceID - different names produce different hashes", "[service_id]")
{
	// Not guaranteed in general, but true for these specific inputs.
	constexpr thx::service::ServiceID a("thx.test.MyService");
	constexpr thx::service::ServiceID b("thx.test.OtherService");

	STATIC_REQUIRE(a.hash() != b.hash());
}

TEST_CASE("ServiceID - name() returns the original string", "[service_id]")
{
	constexpr thx::service::ServiceID id("thx.io.FileService");

	REQUIRE(std::string(id.name()) == "thx.io.FileService");
}

TEST_CASE("ServiceID - empty name", "[service_id]")
{
	constexpr thx::service::ServiceID a("");
	constexpr thx::service::ServiceID b("");

	STATIC_REQUIRE(a == b);
	STATIC_REQUIRE(a.hash() == b.hash());
}

TEST_CASE("ServiceID - usable as unordered_map key", "[service_id]")
{
	std::unordered_map<thx::service::ServiceID, int> registry;

	thx::service::ServiceID file_service("thx.io.FileService");
	thx::service::ServiceID audio_service("thx.io.AudioService");

	registry[file_service] = 1;
	registry[audio_service] = 2;

	REQUIRE(registry.size() == 2);
	REQUIRE(registry[thx::service::ServiceID("thx.io.FileService")] == 1);
	REQUIRE(registry[thx::service::ServiceID("thx.io.AudioService")] == 2);
}

TEST_CASE("ServiceID - lookup with independently constructed key", "[service_id]")
{
	std::unordered_map<thx::service::ServiceID, int> registry;
	registry[thx::service::ServiceID("thx.io.FileService")] = 42;

	// Key constructed from a different pointer but identical content.
	const char name[] = "thx.io.FileService";
	thx::service::ServiceID lookup(name);

	REQUIRE(registry.count(lookup) == 1);
	REQUIRE(registry[lookup] == 42);
}
