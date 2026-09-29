/**
 * @file
 * @brief Unit tests: the parameter list (fixed params, shader group, values by name, host flags).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "plugin/params.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <string>

using namespace ReaShader::Parameters;

namespace
{
	Param shaderParam(const std::string& name, double defaultValue)
	{
		Param p;
		p.name = name;
		p.label = name;
		p.defaultValue = defaultValue;
		p.automatable = true;
		return p;
	}
} // namespace

TEST_SUITE("params")
{
	TEST_CASE("a new list holds only Audio Gain, at 1, automatable")
	{
		ParamList params;

		REQUIRE(params.count() == 1);
		CHECK(params.value(AudioGain) == 1.0);
		CHECK(params.automatableCount() == 1);
		auto gain = params.automatableAt(0);
		REQUIRE(gain);
		CHECK(gain->name == "Audio Gain");
		CHECK(gain->group == Group::Main);
		CHECK_FALSE(params.automatableAt(1));
	}

	TEST_CASE("shader params follow the fixed ones, with values saved by name or their defaults")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5), shaderParam("tint.x", 0.25) }, { { "tint.x", 0.9 } });

		REQUIRE(params.count() == 3);
		auto amount = params.find(DefaultCount);
		auto tintX = params.find(DefaultCount + 1);
		REQUIRE(amount);
		REQUIRE(tintX);
		CHECK(amount->name == "amount");
		CHECK(amount->id == DefaultCount);
		CHECK(amount->group == Group::Shader);
		CHECK(params.value(amount->id) == 0.5); // default
		CHECK(params.value(tintX->id) == 0.9);	// saved
		CHECK(params.automatableCount() == 3);
	}

	TEST_CASE("a new shader's params replace the previous shader's")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("a", 0.1), shaderParam("b", 0.2) }, {});
		params.replaceShaderParams({ shaderParam("c", 0.3) }, {});

		REQUIRE(params.count() == 2);
		CHECK(params.find(DefaultCount)->name == "c");
		CHECK_FALSE(params.find(DefaultCount + 1));

		params.replaceShaderParams({}, {});
		CHECK(params.count() == DefaultCount);
	}

	TEST_CASE("only the first shader params that fit are kept")
	{
		ParamList params;
		std::vector<Param> many;
		for (size_t i = 0; i < ParamList::maxCount + 10; i++)
			many.push_back(shaderParam("p" + std::to_string(i), 0.5));
		params.replaceShaderParams(many, {});

		CHECK(params.count() == ParamList::maxCount);
		CHECK(params.find(ParamList::maxCount - 1)->name == "p" + std::to_string(ParamList::maxCount - DefaultCount - 1));
	}

	TEST_CASE("values go to and from JSON by name; unknown names and non-numbers are ignored")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5) }, {});
		params.setValue(AudioGain, 0.25);

		nlohmann::json saved = params.valuesToJson();
		CHECK(saved == nlohmann::json{ { "Audio Gain", 0.25 }, { "amount", 0.5 } });

		params.valuesFromJson({ { "amount", 0.75 }, { "Audio Gain", "loud" }, { "missing", 1 } });
		CHECK(params.value(DefaultCount) == 0.75);
		CHECK(params.value(AudioGain) == 0.25);
		CHECK(params.count() == 2);
	}

	TEST_CASE("toJson describes every param for the web UI")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5) }, {});

		nlohmann::json list = params.toJson();
		REQUIRE(list.size() == 2);
		CHECK(list[0]["name"] == "Audio Gain");
		CHECK(list[0]["group"] == "main");
		CHECK(list[1]["id"] == DefaultCount);
		CHECK(list[1]["group"] == "shader");
		CHECK(list[1]["value"] == 0.5);
		for (const char* key : { "label", "units", "defaultValue", "minValue", "maxValue" })
		{
			INFO(key);
			CHECK(list[1].contains(key));
		}
	}

	TEST_CASE("each flagged param is taken once for the host, with its latest value")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5) }, {});

		Id id = 0;
		double value = 0;
		CHECK_FALSE(params.takeFlaggedForHost(id, value));

		params.setValue(DefaultCount, 0.6);
		params.flagForHost(DefaultCount);
		params.setValue(DefaultCount, 0.7);
		params.flagForHost(DefaultCount); // twice before the host takes it: one change
		params.flagForHost(AudioGain);

		std::vector<std::pair<Id, double>> taken;
		while (params.takeFlaggedForHost(id, value))
			taken.emplace_back(id, value);

		REQUIRE(taken.size() == 2);
		CHECK(taken[0] == std::pair<Id, double>{ AudioGain, 1.0 });
		CHECK(taken[1] == std::pair<Id, double>{ DefaultCount, 0.7 });
		CHECK_FALSE(params.takeFlaggedForHost(id, value));
	}
}
