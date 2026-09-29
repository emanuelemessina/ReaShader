/**
 * @file
 * @brief Unit tests: the .cube parser, 1D and domain baking, the stored form, base64. No GPU.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/support.h"

#include "render/lut_file.h"
#include "util/base64.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cmath>

using namespace ReaShader;

namespace
{
	gpu::LutData fixture(const char* file)
	{
		return gpu::parseCube(test::readFile(test::repoPath("test/luts") / file), file);
	}

	// The RGB triplet at grid point (r, g, b)
	std::array<float, 3> at(const gpu::LutData& lut, uint32_t r, uint32_t g, uint32_t b)
	{
		size_t i = ((size_t(b) * lut.size + g) * lut.size + r) * 3;
		return { lut.rgb[i], lut.rgb[i + 1], lut.rgb[i + 2] };
	}

	bool near(float a, float b, float tolerance = 1e-5f)
	{
		return std::abs(a - b) <= tolerance;
	}
} // namespace

TEST_SUITE("lut")
{
	TEST_CASE("a 3D .cube is read as written, red fastest")
	{
		gpu::LutData lut = fixture("invert.cube");

		CHECK(lut.title == "Invert");
		REQUIRE(lut.size == 2);
		REQUIRE(lut.rgb.size() == 8 * 3);
		CHECK(at(lut, 0, 0, 0) == std::array<float, 3>{ 1, 1, 1 });
		CHECK(at(lut, 1, 0, 0) == std::array<float, 3>{ 0, 1, 1 });
		CHECK(at(lut, 0, 1, 0) == std::array<float, 3>{ 1, 0, 1 });
		CHECK(at(lut, 0, 0, 1) == std::array<float, 3>{ 1, 1, 0 });
		CHECK(at(lut, 1, 1, 1) == std::array<float, 3>{ 0, 0, 0 });
	}

	TEST_CASE("comments, blank lines, CRLF and unknown keywords are skipped")
	{
		gpu::LutData lut = gpu::parseCube("# header\r\n"
										  "LUT_3D_SIZE 2 # trailing comment\r\n"
										  "LUT_IN_VIDEO_RANGE\r\n"
										  "\r\n"
										  "0 0 0\r\n1 0 0\r\n0 1 0\r\n1 1 0\r\n"
										  "0 0 1\r\n1 0 1\r\n0 1 1\r\n1e0 1.0 1\r\n",
										  "crlf.cube");
		CHECK(lut.title.empty());
		CHECK(lut.rgb == gpu::identityLut(2).rgb);
	}

	TEST_CASE("a 1D LUT is baked into a cube, one curve per channel")
	{
		gpu::LutData lut = fixture("curve_1d.cube");

		REQUIRE(lut.size == gpu::kBakedLutSize);
		uint32_t middle = (gpu::kBakedLutSize - 1) / 2;
		uint32_t quarter = (gpu::kBakedLutSize - 1) / 4;

		// each channel follows only its own input
		std::array<float, 3> m = at(lut, middle, middle, middle);
		CHECK(near(m[0], 0.25f));
		CHECK(near(m[1], 0.5f));
		CHECK(near(m[2], 1.0f));

		std::array<float, 3> q = at(lut, quarter, quarter, quarter);
		CHECK(near(q[0], 0.125f));
		CHECK(near(q[1], 0.25f));
		CHECK(near(q[2], 0.5f));

		std::array<float, 3> mixed = at(lut, middle, 0, gpu::kBakedLutSize - 1);
		CHECK(near(mixed[0], 0.25f));
		CHECK(near(mixed[1], 0.0f));
		CHECK(near(mixed[2], 1.0f));
	}

	TEST_CASE("a DOMAIN other than 0..1 is resampled onto 0..1")
	{
		gpu::LutData identity = fixture("domain.cube");
		REQUIRE(identity.size == 2);
		for (size_t i = 0; i < identity.rgb.size(); i++)
			CHECK(near(identity.rgb[i], gpu::identityLut(2).rgb[i]));

		// DOMAIN_MIN 0.5: inputs below it clamp to the table's first entry
		gpu::LutData clamped = gpu::parseCube("LUT_1D_SIZE 2\nLUT_1D_INPUT_RANGE 0.5 1\n0 0 0\n1 1 1\n", "range.cube");
		CHECK(near(at(clamped, 0, 0, 0)[0], 0.0f));
		uint32_t threeQuarters = (gpu::kBakedLutSize - 1) * 3 / 4;
		CHECK(near(at(clamped, threeQuarters, 0, 0)[0], 0.5f));
	}

	TEST_CASE("errors name the file, and the line when one is at fault")
	{
		CHECK_THROWS_WITH(fixture("broken.cube"), doctest::Contains("broken.cube:6: 'zero' is not a number"));

		CHECK_THROWS_WITH(gpu::parseCube("LUT_3D_SIZE 2\n0 0\n", "short.cube"),
						  doctest::Contains("short.cube:2: expected 3 numbers"));
		CHECK_THROWS_WITH(gpu::parseCube("LUT_3D_SIZE 66\n", "big.cube"),
						  doctest::Contains("big.cube:1: LUT_3D_SIZE must be a number from 2 to 65"));
		CHECK_THROWS_WITH(gpu::parseCube("0 0 0\n", "nosize.cube"), doctest::Contains("no LUT_3D_SIZE or LUT_1D_SIZE"));
		CHECK_THROWS_WITH(gpu::parseCube("LUT_1D_SIZE 2\nLUT_3D_SIZE 2\n", "both.cube"),
						  doctest::Contains("both a 1D and a 3D table"));
		CHECK_THROWS_WITH(gpu::parseCube("LUT_3D_SIZE 2\n0 0 0\n", "count.cube"),
						  doctest::Contains("count.cube: 1 entries, but the size needs 8"));
		CHECK_THROWS_WITH(gpu::parseCube("LUT_1D_SIZE 2\nDOMAIN_MIN 1 0 0\n0 0 0\n1 1 1\n", "domain.cube"),
						  doctest::Contains("DOMAIN_MAX must be above DOMAIN_MIN"));
		CHECK_THROWS_WITH(gpu::parseCube("LUT_1D_SIZE 2\n0 0 0\n1 inf 1\n", "inf.cube"),
						  doctest::Contains("inf.cube:3: 'inf' is not a number"));
	}

	TEST_CASE("the stored JSON form round-trips, at half precision")
	{
		gpu::LutData lut = fixture("curve_1d.cube");
		gpu::LutData stored = gpu::lutFromJson(nlohmann::json::parse(gpu::toJson(lut).dump()));

		CHECK(stored.title == lut.title);
		CHECK(stored.size == lut.size);
		REQUIRE(stored.rgb.size() == lut.rgb.size());
		int mismatches = 0;
		for (size_t i = 0; i < lut.rgb.size(); i++)
			mismatches += !near(stored.rgb[i], lut.rgb[i], 1e-3f);
		CHECK(mismatches == 0);

		CHECK_THROWS(gpu::lutFromJson(nlohmann::json{ { "version", 0 } }));
		nlohmann::json truncated = gpu::toJson(gpu::identityLut(2));
		truncated["data"] = util::base64::encode(std::vector<uint8_t>(10));
		CHECK_THROWS_WITH(gpu::lutFromJson(truncated), doctest::Contains("doesn't match its size"));
	}

	TEST_CASE("base64 round-trips every padding length and rejects invalid text")
	{
		std::vector<uint8_t> bytes = { 0, 1, 2, 250, 251, 252, 253, 254, 255 };
		for (size_t length = 0; length <= bytes.size(); length++)
		{
			INFO("length ", length);
			std::vector<uint8_t> prefix(bytes.begin(), bytes.begin() + (ptrdiff_t)length);
			CHECK(util::base64::decode(util::base64::encode(prefix)) == prefix);
		}
		CHECK(util::base64::encode(std::vector<uint8_t>{ 'M', 'a' }) == "TWE=");

		CHECK_THROWS(util::base64::decode("TWE"));
		CHECK_THROWS(util::base64::decode("TW=E"));
		CHECK_THROWS(util::base64::decode("TWE=TWE="));
		CHECK_THROWS(util::base64::decode("TW*="));
	}
}
