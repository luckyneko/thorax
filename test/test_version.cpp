/*
 *  Created by LuckyNeko on 22/04/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include <catch2/catch_all.hpp>
#include <thx/version.h>
#include <thx/version_type.h>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST_CASE("Version - basic construction", "[version]")
{
	constexpr auto v = thx::Version{1, 2, 3};

	STATIC_REQUIRE(v.major == 1);
	STATIC_REQUIRE(v.minor == 2);
	STATIC_REQUIRE(v.patch == 3);
}

TEST_CASE("Version - default construction yields 0.0.0", "[version]")
{
	constexpr auto v = thx::Version{};

	STATIC_REQUIRE(v.major == 0);
	STATIC_REQUIRE(v.minor == 0);
	STATIC_REQUIRE(v.patch == 0);
}

TEST_CASE("Version - THORAX_VERSION is present", "[version]")
{
	// Smoke test: the generated constant must exist and have the expected major.
	STATIC_REQUIRE(thx::THORAX_VERSION.major == 0);
}

// ---------------------------------------------------------------------------
// Equality
// ---------------------------------------------------------------------------

TEST_CASE("Version - same values are equal", "[version]")
{
	constexpr auto v1 = thx::Version{1, 2, 3};
	constexpr auto v2 = thx::Version{1, 2, 3};

	STATIC_REQUIRE(v1 == v2);
	STATIC_REQUIRE(!(v1 != v2));
}

TEST_CASE("Version - differing fields are not equal", "[version]")
{
	STATIC_REQUIRE(thx::Version{1, 0, 0} != thx::Version{2, 0, 0});
	STATIC_REQUIRE(thx::Version{1, 0, 0} != thx::Version{1, 1, 0});
	STATIC_REQUIRE(thx::Version{1, 0, 0} != thx::Version{1, 0, 1});
}

// ---------------------------------------------------------------------------
// Ordering
// ---------------------------------------------------------------------------

TEST_CASE("Version - major ordering", "[version]")
{
	STATIC_REQUIRE(thx::Version{1, 0, 0} < thx::Version{2, 0, 0});
	STATIC_REQUIRE(thx::Version{2, 0, 0} > thx::Version{1, 0, 0});
	STATIC_REQUIRE(thx::Version{2, 0, 0} > thx::Version{1, 9, 9});
}

TEST_CASE("Version - minor ordering", "[version]")
{
	STATIC_REQUIRE(thx::Version{1, 0, 0} < thx::Version{1, 1, 0});
	STATIC_REQUIRE(thx::Version{1, 1, 0} < thx::Version{1, 2, 0});
}

TEST_CASE("Version - patch ordering", "[version]")
{
	STATIC_REQUIRE(thx::Version{1, 0, 0} < thx::Version{1, 0, 1});
	STATIC_REQUIRE(thx::Version{1, 0, 1} < thx::Version{1, 0, 2});
}

TEST_CASE("Version - all comparison operators", "[version]")
{
	constexpr auto lo = thx::Version{1, 0, 0};
	constexpr auto hi = thx::Version{2, 0, 0};

	STATIC_REQUIRE(lo < hi);
	STATIC_REQUIRE(lo <= hi);
	STATIC_REQUIRE(hi > lo);
	STATIC_REQUIRE(hi >= lo);
	STATIC_REQUIRE(lo <= lo);
	STATIC_REQUIRE(lo >= lo);
}

// ---------------------------------------------------------------------------
// compatible()
// ---------------------------------------------------------------------------

TEST_CASE("Version - compatible: same version is compatible", "[version]")
{
	constexpr auto v = thx::Version{1, 2, 3};
	STATIC_REQUIRE(thx::Version::compatible(v, v));
}

TEST_CASE("Version - compatible: higher minor/patch is compatible", "[version]")
{
	STATIC_REQUIRE(thx::Version::compatible(thx::Version{1, 0, 0}, thx::Version{1, 1, 0}));
	STATIC_REQUIRE(thx::Version::compatible(thx::Version{1, 0, 0}, thx::Version{1, 0, 1}));
	STATIC_REQUIRE(thx::Version::compatible(thx::Version{1, 2, 3}, thx::Version{1, 9, 0}));
}

TEST_CASE("Version - compatible: lower version is not compatible", "[version]")
{
	STATIC_REQUIRE(!thx::Version::compatible(thx::Version{1, 1, 0}, thx::Version{1, 0, 0}));
	STATIC_REQUIRE(!thx::Version::compatible(thx::Version{1, 0, 1}, thx::Version{1, 0, 0}));
}

TEST_CASE("Version - compatible: different major is never compatible", "[version]")
{
	STATIC_REQUIRE(!thx::Version::compatible(thx::Version{1, 0, 0}, thx::Version{2, 0, 0}));
	STATIC_REQUIRE(!thx::Version::compatible(thx::Version{2, 0, 0}, thx::Version{1, 9, 9}));
}

// ---------------------------------------------------------------------------
// pack() / Version(uint32_t)
// ---------------------------------------------------------------------------

TEST_CASE("Version - pack/unpack roundtrip preserves major.minor.patch",
		  "[version][pack]")
{
	// pack() is runtime-only (the overflow check goes through the logging
	// facade); the inverse constructor stays constexpr.
	auto const v      = thx::Version{2, 5, 17};
	auto const packed = v.pack();
	auto const roundt = thx::Version(packed);
	REQUIRE(roundt.major == 2);
	REQUIRE(roundt.minor == 5);
	REQUIRE(roundt.patch == 17);
}

TEST_CASE("Version - pack() uses fixed bit layout",
		  "[version][pack]")
{
	// Documented encoding: (major<<24) | (minor<<16) | patch
	auto const packed = thx::Version{0x12, 0x34, 0x5678}.pack();
	REQUIRE(packed == 0x12345678u);
}

TEST_CASE("Version - Version(0) is the default Version",
		  "[version][pack]")
{
	constexpr auto v = thx::Version(0);
	STATIC_REQUIRE(v.major == 0);
	STATIC_REQUIRE(v.minor == 0);
	STATIC_REQUIRE(v.patch == 0);
}
