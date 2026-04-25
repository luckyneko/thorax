/*
 *  Created by LuckyNeko on 24/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/platform.h>
#include <thx/span.h>
#include <thx/string_view.h>
#include <thx/version_type.h>

// ---------------------------------------------------------------------------
// StringView
// ---------------------------------------------------------------------------

TEST_CASE("StringView - default is empty", "[string_view]")
{
	constexpr thx::StringView sv;
	STATIC_REQUIRE(sv.empty());
	STATIC_REQUIRE(sv.size() == 0);
}

TEST_CASE("StringView - construct from literal", "[string_view]")
{
	constexpr thx::StringView sv("hello");
	STATIC_REQUIRE(sv.size() == 5);
	STATIC_REQUIRE(!sv.empty());
	STATIC_REQUIRE(sv[0] == 'h');
	STATIC_REQUIRE(sv[4] == 'o');
}

TEST_CASE("StringView - construct from pointer and length", "[string_view]")
{
	constexpr thx::StringView sv("hello", 3);
	STATIC_REQUIRE(sv.size() == 3);
	STATIC_REQUIRE(sv[2] == 'l');
}

TEST_CASE("StringView - equality with literal", "[string_view]")
{
	constexpr thx::StringView sv("alpha.1");
	STATIC_REQUIRE(sv == "alpha.1");
	STATIC_REQUIRE(sv != "alpha.2");
}

TEST_CASE("StringView - two empty views are equal", "[string_view]")
{
	constexpr thx::StringView a;
	constexpr thx::StringView b("");
	STATIC_REQUIRE(a == b);
}

TEST_CASE("StringView - ordering", "[string_view]")
{
	constexpr thx::StringView alpha("alpha");
	constexpr thx::StringView beta("beta");
	constexpr thx::StringView alph("alph");

	STATIC_REQUIRE(alpha < beta);
	STATIC_REQUIRE(beta > alpha);
	STATIC_REQUIRE(alph < alpha);   // shorter common prefix loses
	STATIC_REQUIRE(alpha <= alpha);
	STATIC_REQUIRE(alpha >= alpha);
}

TEST_CASE("StringView - explicit conversion to std::string_view", "[string_view]")
{
	thx::StringView sv("world");
	auto stdv = static_cast<std::string_view>(sv);
	REQUIRE(std::string(stdv) == "world");
	REQUIRE(stdv.size() == 5);
}

TEST_CASE("StringView - range-for iteration", "[string_view]")
{
	thx::StringView sv("abc");
	std::string result;
	for (char c : sv)
		result += c;
	REQUIRE(result == "abc");
}

// ---------------------------------------------------------------------------
// Version with StringView fields
// ---------------------------------------------------------------------------

TEST_CASE("Version - pre_release is StringView", "[string_view][version]")
{
	constexpr auto v = thx::make_version(1, 0, 0, "alpha.1");
	STATIC_REQUIRE(v.pre_release == "alpha.1");
	STATIC_REQUIRE(v.pre_release.size() == 7);
}

TEST_CASE("Version - build_metadata is StringView", "[string_view][version]")
{
	constexpr auto v = thx::make_version(1, 0, 0, "", "build.123");
	STATIC_REQUIRE(v.build_metadata == "build.123");
	STATIC_REQUIRE(v.pre_release.empty());
}

// ---------------------------------------------------------------------------
// Span
// ---------------------------------------------------------------------------

TEST_CASE("Span - default is empty", "[span]")
{
	thx::Span<int> sp;
	REQUIRE(sp.empty());
	REQUIRE(sp.size() == 0);
	REQUIRE(sp.data() == nullptr);
}

TEST_CASE("Span - construct from pointer and size", "[span]")
{
	int arr[] = {10, 20, 30};
	thx::Span<int> sp(arr, 3);

	REQUIRE(sp.size() == 3);
	REQUIRE(!sp.empty());
	REQUIRE(sp[0] == 10);
	REQUIRE(sp[2] == 30);
}

TEST_CASE("Span - construct from array", "[span]")
{
	int arr[] = {1, 2, 3, 4, 5};
	thx::Span<int> sp(arr);

	REQUIRE(sp.size() == 5);
	REQUIRE(sp.data() == arr);
}

TEST_CASE("Span - range-for iteration", "[span]")
{
	int arr[] = {1, 2, 3};
	thx::Span<int> sp(arr);

	int sum = 0;
	for (int v : sp)
		sum += v;
	REQUIRE(sum == 6);
}

TEST_CASE("Span<const char> - string span", "[span]")
{
	const char text[] = "hello";
	thx::Span<const char> sp(text, 5);

	REQUIRE(sp.size() == 5);
	REQUIRE(sp[0] == 'h');
}

