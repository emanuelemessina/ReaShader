/**
 * @file
 * @brief Unit tests: the parameter list (the fixed param, nodes' params, ids vs indices, values by name, host flags).
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
	Param param(const std::string& name, double defaultValue, double minValue = 0, double maxValue = 1)
	{
		Param p;
		p.name = name;
		p.label = name;
		p.defaultValue = defaultValue;
		p.minValue = minValue;
		p.maxValue = maxValue;
		p.automatable = true;
		return p;
	}

	// a node's params named "<uid>/<member>", as the plugin names them
	NodeParams node(uint32_t uid, std::vector<std::pair<std::string, double>> members)
	{
		NodeParams result{ uid, {} };
		for (const auto& [member, defaultValue] : members)
			result.params.push_back(param(std::to_string(uid) + "/" + member, defaultValue));
		return result;
	}
} // namespace

TEST_SUITE("params")
{
	TEST_CASE("a new list holds only Audio Gain, at 1 and automatable")
	{
		ParamList params;

		REQUIRE(params.count() == DefaultCount);
		CHECK(params.value(AudioGain) == 1.0);
		CHECK(params.automatableCount() == 1);
		auto gain = params.automatableAt(AudioGainIndex);
		REQUIRE(gain);
		CHECK(gain->id == AudioGain);
		CHECK(gain->name == "Audio Gain");
		CHECK(gain->group == Group::Main);
		CHECK_FALSE(gain->node);
		CHECK_FALSE(params.automatableAt(DefaultCount));
	}

	TEST_CASE("ids are node + slot: the plugin's first, then 64 per node")
	{
		CHECK(AudioGain == 0);
		CHECK(nodeParamId(0, 0) == 1);
		CHECK(nodeParamId(0, 63) == 64);
		CHECK(nodeParamId(1, 0) == 65);
		CHECK(kMaxIds == 1 + kMaxNodes * kNodeSlots);
	}

	TEST_CASE("nodes' params follow the fixed one, in chain order, at their node's ids")
	{
		ParamList params;
		params.replaceNodeParams({ node(3, { { "a", 0.1 }, { "b", 0.2 } }), node(1, { { "mix", 1.0 } }) },
								 { { "3/b", 0.9 } });

		std::vector<Param> list = params.list();
		REQUIRE(list.size() == DefaultCount + 3);
		CHECK(list[DefaultCount].id == nodeParamId(3, 0));
		CHECK(list[DefaultCount + 1].id == nodeParamId(3, 1));
		CHECK(list[DefaultCount + 2].id == nodeParamId(1, 0));
		for (size_t i = DefaultCount; i < list.size(); i++)
		{
			INFO(i);
			CHECK(list[i].group == Group::Node);
		}
		CHECK(list[DefaultCount].node == 3u);
		CHECK(list[DefaultCount + 2].node == 1u);
		CHECK(params.value(nodeParamId(3, 0)) == 0.1); // default
		CHECK(params.value(nodeParamId(3, 1)) == 0.9); // saved
		CHECK(params.automatableCount() == DefaultCount + 3);
	}

	TEST_CASE("nodes moved around keep their ids and their values; only the indices change")
	{
		ParamList params;
		params.replaceNodeParams({ node(3, { { "a", 0.1 } }), node(1, { { "mix", 1.0 } }) }, {});
		params.setValue(nodeParamId(3, 0), 0.4);
		params.setValue(nodeParamId(1, 0), 0.6);

		params.replaceNodeParams({ node(1, { { "mix", 1.0 } }), node(3, { { "a", 0.1 } }) }, {});

		std::vector<Param> list = params.list();
		REQUIRE(list.size() == DefaultCount + 2);
		CHECK(list[DefaultCount].id == nodeParamId(1, 0));
		CHECK(list[DefaultCount + 1].id == nodeParamId(3, 0));
		CHECK(params.value(nodeParamId(3, 0)) == 0.4);
		CHECK(params.value(nodeParamId(1, 0)) == 0.6);
		CHECK(params.valueAt(DefaultCount) == 0.6);
		CHECK(params.valueAt(DefaultCount + 1) == 0.4);
	}

	TEST_CASE("a value is kept only for the same id and name, within the new range; saved values come first")
	{
		ParamList params;
		params.replaceNodeParams({ node(0, { { "a", 0.1 }, { "b", 0.2 }, { "c", 0.3 } }) }, {});
		for (uint32_t slot = 0; slot < 3; slot++)
			params.setValue(nodeParamId(0, slot), 0.8);

		NodeParams next{ 0, { param("0/a", 0.1), param("0/other", 0.2), param("0/c", 0.3) } };
		next.params[0].maxValue = 0.5;
		params.replaceNodeParams({ next }, { { "0/c", 0.25 } });

		CHECK(params.value(nodeParamId(0, 0)) == 0.5);	// kept, clamped to the new range
		CHECK(params.value(nodeParamId(0, 1)) == 0.2);	// another name at that id: its default
		CHECK(params.value(nodeParamId(0, 2)) == 0.25); // saved
	}

	TEST_CASE("ids and indices map both ways")
	{
		ParamList params;
		params.replaceNodeParams({ node(5, { { "a", 0.1 }, { "b", 0.2 } }), node(2, { { "mix", 0.3 } }) }, {});

		std::vector<Param> list = params.list();
		for (size_t i = 0; i < list.size(); i++)
		{
			INFO(i);
			CHECK(params.contains(list[i].id));
			CHECK(params.valueAt(i) == params.value(list[i].id));
			CHECK(params.automatableAt((uint32_t)i)->id == list[i].id);
			CHECK(params.find(list[i].id)->name == list[i].name);
		}
		CHECK(params.valueAt(list.size()) == 0.0); // past the end
	}

	TEST_CASE("ids that aren't params are unknown")
	{
		ParamList params;
		params.replaceNodeParams({ node(0, { { "a", 0.1 } }) }, {});

		for (Id id : { nodeParamId(0, 1), nodeParamId(1, 0), Id(42), kMaxIds, Id(-1) })
		{
			INFO(id);
			CHECK_FALSE(params.contains(id));
			CHECK_FALSE(params.find(id));
		}
		CHECK(params.contains(nodeParamId(0, 0)));

		params.replaceNodeParams({}, {});
		CHECK(params.count() == DefaultCount);
		CHECK_FALSE(params.contains(nodeParamId(0, 0)));
	}

	TEST_CASE("at most 64 params per node, and 256 in all")
	{
		ParamList params;
		std::vector<NodeParams> nodes;
		for (uint32_t uid = 0; uid < 5; uid++)
		{
			std::vector<std::pair<std::string, double>> members;
			for (size_t i = 0; i < kNodeSlots + 10; i++)
				members.emplace_back("p" + std::to_string(i), 0.5);
			nodes.push_back(node(uid, members));
		}
		params.replaceNodeParams(nodes, {});

		CHECK(params.count() == ParamList::maxCount);
		CHECK(params.find(nodeParamId(0, kNodeSlots - 1))->name == "0/p63");
		CHECK(params.find(nodeParamId(1, 0))->name == "1/p0"); // node 0's extra params were dropped
		// 1 + 3 * 64 = 193 params before node 3, which gets the remaining 63
		CHECK(params.list().back().id == nodeParamId(3, 62));
		CHECK_FALSE(params.contains(nodeParamId(4, 0)));
	}

	TEST_CASE("values go to and from JSON by name; unknown names and non-numbers are ignored")
	{
		ParamList params;
		params.replaceNodeParams({ node(0, { { "amount", 0.5 } }) }, {});
		params.setValue(AudioGain, 0.25);

		nlohmann::json saved = params.valuesToJson();
		CHECK(saved == nlohmann::json{ { "Audio Gain", 0.25 }, { "0/amount", 0.5 } });

		params.valuesFromJson({ { "0/amount", 0.75 }, { "Audio Gain", "loud" }, { "missing", 1 } });
		CHECK(params.value(nodeParamId(0, 0)) == 0.75);
		CHECK(params.value(AudioGain) == 0.25);
		CHECK(params.count() == DefaultCount + 1);
	}

	TEST_CASE("toJson describes every param for the web UI, in list order, with their ids and nodes")
	{
		ParamList params;
		params.replaceNodeParams({ node(2, { { "amount", 0.5 } }) }, {});

		nlohmann::json list = params.toJson();
		REQUIRE(list.size() == DefaultCount + 1);
		CHECK(list[AudioGainIndex]["id"] == AudioGain);
		CHECK(list[AudioGainIndex]["name"] == "Audio Gain");
		CHECK(list[AudioGainIndex]["group"] == "main");
		CHECK(list[AudioGainIndex]["node"].is_null());
		CHECK(list[DefaultCount]["id"] == nodeParamId(2, 0));
		CHECK(list[DefaultCount]["group"] == "node");
		CHECK(list[DefaultCount]["node"] == 2);
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
		params.replaceNodeParams({ node(0, { { "amount", 0.5 } }), node(1, { { "mix", 1.0 } }) }, {});
		const Id amount = nodeParamId(0, 0), mix = nodeParamId(1, 0);

		Id id = 0;
		double value = 0;
		CHECK_FALSE(params.takeFlaggedForHost(id, value));

		params.setValue(mix, 0.6);
		params.flagForHost(mix);
		params.setValue(mix, 0.7);
		params.flagForHost(mix); // twice before the host takes it: one change
		params.flagForHost(amount);
		params.flagForHost(42); // not a param: ignored

		std::vector<std::pair<Id, double>> taken;
		while (params.takeFlaggedForHost(id, value))
			taken.emplace_back(id, value);

		REQUIRE(taken.size() == 2);
		CHECK(taken[0] == std::pair<Id, double>{ amount, 0.5 });
		CHECK(taken[1] == std::pair<Id, double>{ mix, 0.7 });
		CHECK_FALSE(params.takeFlaggedForHost(id, value));
	}

	TEST_CASE("a flag on a param that goes away isn't handed to the host")
	{
		ParamList params;
		params.replaceNodeParams({ node(0, { { "a", 0.1 } }) }, {});
		params.flagForHost(nodeParamId(0, 0));
		params.replaceNodeParams({}, {});
		params.replaceNodeParams({ node(0, { { "b", 0.2 } }) }, {}); // the same id again

		Id id = 0;
		double value = 0;
		CHECK_FALSE(params.takeFlaggedForHost(id, value));
	}
}
