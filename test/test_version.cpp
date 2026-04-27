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

TEST_CASE("Version - basic construction via make_version", "[version]")
{
	constexpr auto v = thx::make_version(1, 2, 3);

	STATIC_REQUIRE(v.major == 1);
	STATIC_REQUIRE(v.minor == 2);
	STATIC_REQUIRE(v.patch == 3);
	STATIC_REQUIRE(v.pre_release.empty());
	STATIC_REQUIRE(v.build_metadata.empty());
}

TEST_CASE("Version - construction with pre-release", "[version]")
{
	constexpr auto v = thx::make_version(1, 0, 0, "alpha.1");

	STATIC_REQUIRE(v.major == 1);
	STATIC_REQUIRE(v.minor == 0);
	STATIC_REQUIRE(v.patch == 0);
	STATIC_REQUIRE(v.pre_release == "alpha.1");
	STATIC_REQUIRE(v.build_metadata.empty());
}

TEST_CASE("Version - construction with build metadata", "[version]")
{
	constexpr auto v = thx::make_version(1, 0, 0, "", "build.123.abc");

	STATIC_REQUIRE(v.pre_release.empty());
	STATIC_REQUIRE(v.build_metadata == "build.123.abc");
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
	constexpr auto v1 = thx::make_version(1, 2, 3);
	constexpr auto v2 = thx::make_version(1, 2, 3);

	STATIC_REQUIRE(v1 == v2);
	STATIC_REQUIRE(!(v1 != v2));
}

TEST_CASE("Version - differing fields are not equal", "[version]")
{
	STATIC_REQUIRE(thx::make_version(1, 0, 0) != thx::make_version(2, 0, 0));
	STATIC_REQUIRE(thx::make_version(1, 0, 0) != thx::make_version(1, 1, 0));
	STATIC_REQUIRE(thx::make_version(1, 0, 0) != thx::make_version(1, 0, 1));
}

TEST_CASE("Version - build metadata is ignored for equality", "[version]")
{
	// Semver 2.0 §10: build metadata does not affect version precedence.
	constexpr auto v1 = thx::make_version(1, 0, 0, "", "build.1");
	constexpr auto v2 = thx::make_version(1, 0, 0, "", "build.999");
	constexpr auto v3 = thx::make_version(1, 0, 0);

	STATIC_REQUIRE(v1 == v2);
	STATIC_REQUIRE(v1 == v3);
}

TEST_CASE("Version - pre-release is included in equality", "[version]")
{
	constexpr auto release = thx::make_version(1, 0, 0);
	constexpr auto pre = thx::make_version(1, 0, 0, "alpha");

	STATIC_REQUIRE(release != pre);
}

// ---------------------------------------------------------------------------
// Ordering — numeric fields
// ---------------------------------------------------------------------------

TEST_CASE("Version - major ordering", "[version]")
{
	STATIC_REQUIRE(thx::make_version(1, 0, 0) < thx::make_version(2, 0, 0));
	STATIC_REQUIRE(thx::make_version(2, 0, 0) > thx::make_version(1, 0, 0));
	STATIC_REQUIRE(thx::make_version(2, 0, 0) > thx::make_version(1, 9, 9));
}

TEST_CASE("Version - minor ordering", "[version]")
{
	STATIC_REQUIRE(thx::make_version(1, 0, 0) < thx::make_version(1, 1, 0));
	STATIC_REQUIRE(thx::make_version(1, 1, 0) < thx::make_version(1, 2, 0));
}

TEST_CASE("Version - patch ordering", "[version]")
{
	STATIC_REQUIRE(thx::make_version(1, 0, 0) < thx::make_version(1, 0, 1));
	STATIC_REQUIRE(thx::make_version(1, 0, 1) < thx::make_version(1, 0, 2));
}

TEST_CASE("Version - all comparison operators", "[version]")
{
	constexpr auto lo = thx::make_version(1, 0, 0);
	constexpr auto hi = thx::make_version(2, 0, 0);

	STATIC_REQUIRE(lo < hi);
	STATIC_REQUIRE(lo <= hi);
	STATIC_REQUIRE(hi > lo);
	STATIC_REQUIRE(hi >= lo);
	STATIC_REQUIRE(lo <= lo);
	STATIC_REQUIRE(lo >= lo);
}

// ---------------------------------------------------------------------------
// Ordering — pre-release (semver 2.0 §11 examples)
// ---------------------------------------------------------------------------

TEST_CASE("Version - pre-release has lower precedence than release", "[version]")
{
	STATIC_REQUIRE(thx::make_version(1, 0, 0, "alpha") < thx::make_version(1, 0, 0));
	STATIC_REQUIRE(thx::make_version(1, 0, 0, "0") < thx::make_version(1, 0, 0));
	STATIC_REQUIRE(thx::make_version(1, 0, 0, "rc.1") < thx::make_version(1, 0, 0));
}

TEST_CASE("Version - semver 2.0 precedence chain from spec", "[version]")
{
	// From semver.org §11:
	// 1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta
	//   < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0
	constexpr auto alpha = thx::make_version(1, 0, 0, "alpha");
	constexpr auto alpha_1 = thx::make_version(1, 0, 0, "alpha.1");
	constexpr auto alpha_beta = thx::make_version(1, 0, 0, "alpha.beta");
	constexpr auto beta = thx::make_version(1, 0, 0, "beta");
	constexpr auto beta_2 = thx::make_version(1, 0, 0, "beta.2");
	constexpr auto beta_11 = thx::make_version(1, 0, 0, "beta.11");
	constexpr auto rc_1 = thx::make_version(1, 0, 0, "rc.1");
	constexpr auto release = thx::make_version(1, 0, 0);

	STATIC_REQUIRE(alpha < alpha_1);
	STATIC_REQUIRE(alpha_1 < alpha_beta);
	STATIC_REQUIRE(alpha_beta < beta);
	STATIC_REQUIRE(beta < beta_2);
	STATIC_REQUIRE(beta_2 < beta_11); // numeric: 2 < 11
	STATIC_REQUIRE(beta_11 < rc_1);
	STATIC_REQUIRE(rc_1 < release);
}

TEST_CASE("Version - numeric identifier compared as integer not string", "[version]")
{
	// Numeric comparison: 2 < 11, not lexicographic "11" < "2".
	constexpr auto v2 = thx::make_version(1, 0, 0, "pre.2");
	constexpr auto v11 = thx::make_version(1, 0, 0, "pre.11");

	STATIC_REQUIRE(v2 < v11);
}

TEST_CASE("Version - numeric identifier has lower precedence than alphanumeric", "[version]")
{
	// Semver 2.0 §11.4.1: numeric < alphanumeric when compared.
	constexpr auto num = thx::make_version(1, 0, 0, "1");
	constexpr auto alpha = thx::make_version(1, 0, 0, "alpha");

	STATIC_REQUIRE(num < alpha);
}

TEST_CASE("Version - fewer identifiers means lower precedence", "[version]")
{
	constexpr auto shorter = thx::make_version(1, 0, 0, "alpha");
	constexpr auto longer = thx::make_version(1, 0, 0, "alpha.1");

	STATIC_REQUIRE(shorter < longer);
}

// ---------------------------------------------------------------------------
// compatible()
// ---------------------------------------------------------------------------

TEST_CASE("Version - compatible: same version is compatible", "[version]")
{
	constexpr auto v = thx::make_version(1, 2, 3);
	STATIC_REQUIRE(thx::compatible(v, v));
}

TEST_CASE("Version - compatible: higher minor/patch is compatible", "[version]")
{
	STATIC_REQUIRE(thx::compatible(thx::make_version(1, 0, 0), thx::make_version(1, 1, 0)));
	STATIC_REQUIRE(thx::compatible(thx::make_version(1, 0, 0), thx::make_version(1, 0, 1)));
	STATIC_REQUIRE(thx::compatible(thx::make_version(1, 2, 3), thx::make_version(1, 9, 0)));
}

TEST_CASE("Version - compatible: lower version is not compatible", "[version]")
{
	STATIC_REQUIRE(!thx::compatible(thx::make_version(1, 1, 0), thx::make_version(1, 0, 0)));
	STATIC_REQUIRE(!thx::compatible(thx::make_version(1, 0, 1), thx::make_version(1, 0, 0)));
}

TEST_CASE("Version - compatible: different major is never compatible", "[version]")
{
	STATIC_REQUIRE(!thx::compatible(thx::make_version(1, 0, 0), thx::make_version(2, 0, 0)));
	STATIC_REQUIRE(!thx::compatible(thx::make_version(2, 0, 0), thx::make_version(1, 9, 9)));
}

TEST_CASE("Version - compatible: release satisfies pre-release requirement", "[version]")
{
	// provided (release) > required (pre-release), same major — compatible.
	STATIC_REQUIRE(thx::compatible(thx::make_version(1, 0, 0, "alpha"),
								   thx::make_version(1, 0, 0)));
}

TEST_CASE("Version - compatible: pre-release does not satisfy release requirement", "[version]")
{
	// provided (pre-release) < required (release) — not compatible.
	STATIC_REQUIRE(!thx::compatible(thx::make_version(1, 0, 0),
									thx::make_version(1, 0, 0, "alpha")));
}

// ---------------------------------------------------------------------------
// pack_version / unpack_version
// ---------------------------------------------------------------------------

TEST_CASE("Version - pack/unpack roundtrip preserves major.minor.patch",
		  "[version][pack]")
{
	constexpr auto v       = thx::make_version(2, 5, 17);
	constexpr auto packed  = thx::pack_version(v);
	constexpr auto roundt  = thx::unpack_version(packed);
	STATIC_REQUIRE(roundt.major == 2);
	STATIC_REQUIRE(roundt.minor == 5);
	STATIC_REQUIRE(roundt.patch == 17);
}

TEST_CASE("Version - pack_version uses fixed bit layout",
		  "[version][pack]")
{
	// Documented encoding: (major<<24) | (minor<<16) | patch
	constexpr auto packed = thx::pack_version(thx::make_version(0x12, 0x34, 0x5678));
	STATIC_REQUIRE(packed == 0x12345678u);
}

TEST_CASE("Version - pack_version drops pre_release and build_metadata",
		  "[version][pack]")
{
	// Pre-release / build metadata don't fit in the packed encoding.
	constexpr auto a = thx::pack_version(thx::make_version(1, 2, 3));
	constexpr auto b = thx::pack_version(thx::make_version(1, 2, 3, "alpha", "build"));
	STATIC_REQUIRE(a == b);
}

TEST_CASE("Version - unpack_version of zero is the default Version",
		  "[version][pack]")
{
	constexpr auto v = thx::unpack_version(0);
	STATIC_REQUIRE(v.major == 0);
	STATIC_REQUIRE(v.minor == 0);
	STATIC_REQUIRE(v.patch == 0);
}
