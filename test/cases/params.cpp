/**
 * @file
 * @brief Unit tests: the parameter list (fixed params, shader group, ids vs indices, values by name, host flags).
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
	TEST_CASE("a new list holds the fixed params, Audio Gain and LUT Mix, both at 1 and automatable")
	{
		ParamList params;

		REQUIRE(params.count() == DefaultCount);
		CHECK(params.value(AudioGain) == 1.0);
		CHECK(params.value(LutMix) == 1.0);
		CHECK(params.automatableCount() == 2);
		auto gain = params.automatableAt(AudioGainIndex);
		REQUIRE(gain);
		CHECK(gain->id == AudioGain);
		CHECK(gain->name == "Audio Gain");
		CHECK(gain->group == Group::Main);
		auto mix = params.automatableAt(LutMixIndex);
		REQUIRE(mix);
		CHECK(mix->id == LutMix);
		CHECK(mix->name == "LUT Mix");
		CHECK(mix->group == Group::Lut);
		CHECK_FALSE(params.automatableAt(DefaultCount));
	}

	TEST_CASE("ids are node + slot: the plugin's first, then 64 per node")
	{
		CHECK(AudioGain == 0);
		CHECK(nodeParamId(kShaderNode, 0) == 1);
		CHECK(nodeParamId(kShaderNode, 63) == 64);
		CHECK(LutMix == nodeParamId(kLutNode, 0));
		CHECK(LutMix == 65);
		CHECK(kMaxIds == 1 + kMaxNodes * kNodeSlots);
	}

	TEST_CASE("shader params follow the fixed ones, with values saved by name or their defaults")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5), shaderParam("tint.x", 0.25) }, { { "tint.x", 0.9 } });

		REQUIRE(params.count() == DefaultCount + 2);
		auto amount = params.find(nodeParamId(kShaderNode, 0));
		auto tintX = params.find(nodeParamId(kShaderNode, 1));
		REQUIRE(amount);
		REQUIRE(tintX);
		CHECK(amount->name == "amount");
		CHECK(amount->group == Group::Shader);
		CHECK(tintX->name == "tint.x");
		CHECK(params.value(amount->id) == 0.5); // default
		CHECK(params.value(tintX->id) == 0.9);	// saved
		CHECK(params.automatableCount() == DefaultCount + 2);
	}

	TEST_CASE("ids and indices map both ways: list order is the index, values are by id")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("a", 0.1), shaderParam("b", 0.2) }, {});
		params.setValue(LutMix, 0.3);

		std::vector<Param> list = params.list();
		REQUIRE(list.size() == DefaultCount + 2);
		CHECK(list[AudioGainIndex].id == AudioGain);
		CHECK(list[LutMixIndex].id == LutMix);
		CHECK(list[DefaultCount].id == nodeParamId(kShaderNode, 0));
		CHECK(list[DefaultCount + 1].id == nodeParamId(kShaderNode, 1));
		for (size_t i = 0; i < list.size(); i++)
		{
			INFO(i);
			CHECK(params.contains(list[i].id));
			CHECK(params.valueAt(i) == params.value(list[i].id));
			CHECK(params.automatableAt((uint32_t)i)->id == list[i].id);
		}
		CHECK(params.valueAt(LutMixIndex) == 0.3);
		CHECK(params.valueAt(list.size()) == 0.0); // past the end
	}

	TEST_CASE("ids that aren't params are unknown")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("a", 0.1) }, {});

		for (Id id : { nodeParamId(kShaderNode, 1), nodeParamId(kLutNode, 1), Id(42), kMaxIds, Id(-1) })
		{
			INFO(id);
			CHECK_FALSE(params.contains(id));
			CHECK_FALSE(params.find(id));
		}
		CHECK(params.contains(nodeParamId(kShaderNode, 0)));
	}

	TEST_CASE("a new shader's params replace the previous shader's, at the same ids")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("a", 0.1), shaderParam("b", 0.2) }, {});
		params.replaceShaderParams({ shaderParam("c", 0.3) }, {});

		REQUIRE(params.count() == DefaultCount + 1);
		CHECK(params.find(nodeParamId(kShaderNode, 0))->name == "c");
		CHECK_FALSE(params.find(nodeParamId(kShaderNode, 1)));
		CHECK(params.find(LutMix)->name == "LUT Mix"); // the fixed ones keep their ids

		params.replaceShaderParams({}, {});
		CHECK(params.count() == DefaultCount);
		CHECK_FALSE(params.contains(nodeParamId(kShaderNode, 0)));
	}

	TEST_CASE("a shader keeps at most one node's worth of params")
	{
		ParamList params;
		std::vector<Param> many;
		for (size_t i = 0; i < kNodeSlots + 10; i++)
			many.push_back(shaderParam("p" + std::to_string(i), 0.5));
		params.replaceShaderParams(many, {});

		CHECK(params.count() == DefaultCount + kNodeSlots);
		CHECK(params.find(nodeParamId(kShaderNode, kNodeSlots - 1))->name == "p" + std::to_string(kNodeSlots - 1));
		CHECK(params.find(nodeParamId(kShaderNode, kNodeSlots))->name == "LUT Mix"); // the next node's first id
	}

	TEST_CASE("values go to and from JSON by name; unknown names and non-numbers are ignored")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5) }, {});
		params.setValue(AudioGain, 0.25);

		nlohmann::json saved = params.valuesToJson();
		CHECK(saved == nlohmann::json{ { "Audio Gain", 0.25 }, { "LUT Mix", 1.0 }, { "amount", 0.5 } });

		params.valuesFromJson({ { "amount", 0.75 }, { "Audio Gain", "loud" }, { "missing", 1 } });
		CHECK(params.value(nodeParamId(kShaderNode, 0)) == 0.75);
		CHECK(params.value(AudioGain) == 0.25);
		CHECK(params.count() == DefaultCount + 1);
	}

	TEST_CASE("toJson describes every param for the web UI, in list order, with their ids")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5) }, {});

		nlohmann::json list = params.toJson();
		REQUIRE(list.size() == DefaultCount + 1);
		CHECK(list[AudioGainIndex]["id"] == AudioGain);
		CHECK(list[AudioGainIndex]["name"] == "Audio Gain");
		CHECK(list[AudioGainIndex]["group"] == "main");
		CHECK(list[LutMixIndex]["id"] == LutMix);
		CHECK(list[LutMixIndex]["name"] == "LUT Mix");
		CHECK(list[LutMixIndex]["group"] == "lut");
		CHECK(list[DefaultCount]["id"] == nodeParamId(kShaderNode, 0));
		CHECK(list[DefaultCount]["group"] == "shader");
		CHECK(list[DefaultCount]["value"] == 0.5);
		for (const char* key : { "label", "units", "defaultValue", "minValue", "maxValue" })
		{
			INFO(key);
			CHECK(list[DefaultCount].contains(key));
		}
	}

	TEST_CASE("each flagged param is taken once for the host, by id, with its latest value")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("amount", 0.5) }, {});
		const Id amount = nodeParamId(kShaderNode, 0);

		Id id = 0;
		double value = 0;
		CHECK_FALSE(params.takeFlaggedForHost(id, value));

		params.setValue(amount, 0.6);
		params.flagForHost(amount);
		params.setValue(amount, 0.7);
		params.flagForHost(amount); // twice before the host takes it: one change
		params.flagForHost(LutMix);
		params.flagForHost(42); // not a param: ignored

		std::vector<std::pair<Id, double>> taken;
		while (params.takeFlaggedForHost(id, value))
			taken.emplace_back(id, value);

		REQUIRE(taken.size() == 2);
		CHECK(taken[0] == std::pair<Id, double>{ LutMix, 1.0 });
		CHECK(taken[1] == std::pair<Id, double>{ amount, 0.7 });
		CHECK_FALSE(params.takeFlaggedForHost(id, value));
	}

	TEST_CASE("a flag on a param that goes away isn't handed to the host")
	{
		ParamList params;
		params.replaceShaderParams({ shaderParam("a", 0.1) }, {});
		params.flagForHost(nodeParamId(kShaderNode, 0));
		params.replaceShaderParams({}, {});
		params.replaceShaderParams({ shaderParam("b", 0.2) }, {}); // the same id again

		Id id = 0;
		double value = 0;
		CHECK_FALSE(params.takeFlaggedForHost(id, value));
	}
}
