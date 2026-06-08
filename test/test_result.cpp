/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/result.h>

#include <memory>

// ---------------------------------------------------------------------------
// Result<T, Error>
// ---------------------------------------------------------------------------

TEST_CASE("Result<int> - ok", "[result]")
{
	auto r = thx::Result<int>::ok(42);
	REQUIRE(r.isOk());
	REQUIRE(!r.isErr());
	REQUIRE(bool(r));
	REQUIRE(r.value() == 42);
}

TEST_CASE("Result<int> - err", "[result]")
{
	auto r = thx::Result<int>::err({thx::ErrorCode::NotLoaded, "nope"});
	REQUIRE(r.isErr());
	REQUIRE(!r.isOk());
	REQUIRE(!bool(r));
	REQUIRE(r.error().code == thx::ErrorCode::NotLoaded);
	REQUIRE(r.error().message == "nope");
}

TEST_CASE("Result<void> - ok", "[result]")
{
	auto r = thx::Result<void>::ok();
	REQUIRE(r.isOk());
	REQUIRE(bool(r));
}

TEST_CASE("Result<void> - err", "[result]")
{
	auto r = thx::Result<void>::err({thx::ErrorCode::FileNotFound, "missing"});
	REQUIRE(r.isErr());
	REQUIRE(!bool(r));
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
}

TEST_CASE("Result<int> - valueOr returns the value when ok", "[result]")
{
	auto r = thx::Result<int>::ok(42);
	REQUIRE(r.valueOr(-1) == 42);
}

TEST_CASE("Result<int> - valueOr returns the fallback when err", "[result]")
{
	auto r = thx::Result<int>::err({thx::ErrorCode::NotLoaded, "nope"});
	REQUIRE(r.valueOr(-1) == -1);
}

TEST_CASE("Result<int> - map transforms the value when ok", "[result]")
{
	auto r = thx::Result<int>::ok(2).map([](int n)
										 { return n * 10; });
	REQUIRE(r.isOk());
	REQUIRE(r.value() == 20);
}

TEST_CASE("Result<int> - map can change the contained type", "[result]")
{
	auto r = thx::Result<int>::ok(7).map([](int n)
										 { return std::to_string(n); });
	REQUIRE(r.isOk());
	REQUIRE(r.value() == "7");
}

TEST_CASE("Result<int> - map propagates the error unchanged", "[result]")
{
	auto r = thx::Result<int>::err({thx::ErrorCode::FileNotFound, "missing"})
				 .map([](int n)
					  { return n * 10; });
	REQUIRE(r.isErr());
	REQUIRE(r.error().code == thx::ErrorCode::FileNotFound);
	REQUIRE(r.error().message == "missing");
}

TEST_CASE("Result - map rvalue overload moves a move-only value", "[result]")
{
	auto r = thx::Result<std::unique_ptr<int>>::ok(std::make_unique<int>(5))
				 .map([](std::unique_ptr<int> p)
					  { return *p + 1; });
	REQUIRE(r.isOk());
	REQUIRE(r.value() == 6);
}
