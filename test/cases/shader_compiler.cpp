/**
 * @file
 * @brief Unit tests: the shader contract (compiling, Params reflection, //@param, errors, stored form). No GPU.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/support.h"

#include "render/shader_compiler.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

using namespace ReaShader;

TEST_SUITE("shader_compiler")
{
	TEST_CASE("the shipped examples compile")
	{
		for (const char* example : { "brightness.frag", "tint.frag", "wave.frag", "pixelate.frag" })
		{
			INFO(example);
			CHECK_NOTHROW(gpu::compileShader(test::readFile(test::repoPath("src/shaders/examples") / example), example));
		}
	}

	TEST_CASE("Params members become one slider per component, at their block offsets")
	{
		gpu::CompiledShader shader = gpu::compileShader(
			"uniform Params { float amount; vec3 tint; };\nvoid main() { fragColor = vec4(tint, amount); }\n",
			"custom.glsl");

		REQUIRE(shader.params.size() == 4);
		CHECK(shader.params[0].name == "amount");
		CHECK(shader.params[1].name == "tint.x");
		CHECK(shader.params[3].name == "tint.z");
		// std140: vec3 aligned to 16 bytes
		CHECK(shader.params[0].offset == 0);
		CHECK(shader.params[1].offset == 16);
		CHECK(shader.params[3].offset == 24);
		CHECK(shader.paramsSize == 32);
	}

	TEST_CASE("//@param sets label, default and range; members without one get the defaults")
	{
		gpu::CompiledShader shader = gpu::compileShader("//@param amount 'The Amount' 2 0 10\n"
														"uniform Params { float amount; vec2 tint; };\n"
														"void main() { fragColor = vec4(tint, amount, 1); }\n",
														"annotated.glsl");

		const gpu::ShaderParamField& amount = shader.params.at(0);
		CHECK(amount.label == "The Amount");
		CHECK(amount.defaultValue == 2);
		CHECK(amount.minValue == 0);
		CHECK(amount.maxValue == 10);

		const gpu::ShaderParamField& tintX = shader.params.at(1);
		CHECK(tintX.label == "tint.x");
		CHECK(tintX.defaultValue == 0.5f);
		CHECK(tintX.minValue == 0);
		CHECK(tintX.maxValue == 1);
	}

	TEST_CASE("iLut applies iChannel1 (the LUT)")
	{
		CHECK_NOTHROW(gpu::compileShader(
			"void main() { vec4 c = texture(iChannel0, uv); fragColor = vec4(iLut(c.rgb), c.a); }\n", "ilut.glsl"));
	}

	TEST_CASE("compile errors point at the user's own line")
	{
		CHECK_THROWS_WITH(gpu::compileShader(test::readFile(test::repoPath("test/shaders/broken.frag")), "broken.frag"),
						  doctest::Contains("broken.frag:7"));
	}

	TEST_CASE("resources other than iChannel0, iChannel1 and Params are rejected")
	{
		CHECK_THROWS_WITH(
			gpu::compileShader("uniform sampler2D other;\nvoid main() { fragColor = texture(other, uv); }\n", "sampler.glsl"),
			doctest::Contains("'other': only iChannel0, iChannel1 and one uniform block"));

		// a declaration at iChannel1's binding would alias it
		CHECK_THROWS(gpu::compileShader(
			"layout(binding = 2) uniform sampler2D alias;\nvoid main() { fragColor = texture(alias, uv); }\n",
			"alias.glsl"));

		CHECK_THROWS_WITH(gpu::compileShader("layout(set = 1) uniform Params { float amount; };\n"
											 "void main() { fragColor = vec4(amount); }\n",
											 "set1.glsl"),
						  doctest::Contains("'Params': only descriptor set 0"));
	}

	TEST_CASE("the stored JSON form round-trips")
	{
		gpu::CompiledShader shader = gpu::compileShader("//@param amount 'Amount' 0.3 0 2\n"
														"uniform Params { float amount; vec2 tint; };\n"
														"void main() { fragColor = vec4(tint, amount, 1); }\n",
														"stored.glsl");
		gpu::CompiledShader stored = gpu::fromJson(nlohmann::json::parse(gpu::toJson(shader).dump()));

		CHECK(stored.spirv == shader.spirv);
		CHECK(stored.paramsSize == shader.paramsSize);
		REQUIRE(stored.params.size() == shader.params.size());
		for (size_t i = 0; i < shader.params.size(); i++)
		{
			INFO("param ", i);
			CHECK(stored.params[i].name == shader.params[i].name);
			CHECK(stored.params[i].label == shader.params[i].label);
			CHECK(stored.params[i].defaultValue == shader.params[i].defaultValue);
			CHECK(stored.params[i].minValue == shader.params[i].minValue);
			CHECK(stored.params[i].maxValue == shader.params[i].maxValue);
			CHECK(stored.params[i].offset == shader.params[i].offset);
		}

		CHECK_THROWS(gpu::fromJson(nlohmann::json{ { "version", 0 } }));
	}
}
