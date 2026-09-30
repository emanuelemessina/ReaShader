/**
 * @file
 * @brief Unit tests: the parameter list (a fixed list: Audio Gain and every node slot, values by name, host flags).
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
	TEST_CASE("ids are node + slot: Audio Gain first, then 40 per node, 641 in all")
	{
		CHECK(AudioGain == 0);
		CHECK(nodeParamId(0, 0) == 1);
		CHECK(nodeParamId(0, 39) == 40);
		CHECK(nodeParamId(1, 0) == 41);
		CHECK(kParamCount == 1 + kMaxNodes * kNodeSlots);
	}

	TEST_CASE("a new list has every id: Audio Gain at 1, every node slot unused (no name, no node)")
	{
		ParamList params;

		CHECK(params.value(AudioGain) == 1.0);
		Param gain = params.at(AudioGain);
		CHECK(gain.name == "Audio Gain");
		CHECK(gain.group == Group::Main);
		CHECK(gain.used());
		CHECK(params.used(AudioGain));

		for (Id id : { nodeParamId(0, 0), nodeParamId(7, 13), kParamCount - 1 })
		{
			INFO(id);
			Param slot = params.at(id);
			CHECK(slot.id == id);
			CHECK(slot.name.empty());
			CHECK(slot.label.empty());
			CHECK(slot.group == Group::Node);
			CHECK_FALSE(slot.used());
			CHECK_FALSE(params.used(id));
			CHECK_FALSE(params.find(id));
		}
		REQUIRE(params.list().size() == 1);
		CHECK(params.list()[0].id == AudioGain);
	}

	TEST_CASE("nodes' params go to their node's slots, with values saved by name or their defaults")
	{
		ParamList params;
		std::vector<Id> gone = params.replaceNodeParams(
			{ node(3, { { "a", 0.1 }, { "b", 0.2 } }), node(1, { { "mix", 1.0 } }) }, { { "3/b", 0.9 } });
		CHECK(gone.empty());

		std::vector<Param> list = params.list(); // in id order, not chain order
		REQUIRE(list.size() == 4);
		CHECK(list[1].id == nodeParamId(1, 0));
		CHECK(list[2].id == nodeParamId(3, 0));
		CHECK(list[3].id == nodeParamId(3, 1));
		CHECK(list[1].node == 1u);
		CHECK(list[2].node == 3u);
		CHECK(list[2].group == Group::Node);
		CHECK(params.used(nodeParamId(3, 1)));
		CHECK_FALSE(params.used(nodeParamId(3, 2)));
		CHECK(params.value(nodeParamId(3, 0)) == 0.1); // default
		CHECK(params.value(nodeParamId(3, 1)) == 0.9); // saved
	}

	TEST_CASE("nodes moved around keep their ids and values, and nothing is gone")
	{
		ParamList params;
		params.replaceNodeParams({ node(3, { { "a", 0.1 } }), node(1, { { "mix", 1.0 } }) }, {});
		params.setValue(nodeParamId(3, 0), 0.4);
		params.setValue(nodeParamId(1, 0), 0.6);

		std::vector<Id> gone = params.replaceNodeParams({ node(1, { { "mix", 1.0 } }), node(3, { { "a", 0.1 } }) }, {});

		CHECK(gone.empty());
		CHECK(params.value(nodeParamId(3, 0)) == 0.4);
		CHECK(params.value(nodeParamId(1, 0)) == 0.6);
	}

	TEST_CASE("a value is kept only for the same id and name, within the new range; saved values come first")
	{
		ParamList params;
		params.replaceNodeParams({ node(0, { { "a", 0.1 }, { "b", 0.2 }, { "c", 0.3 } }) }, {});
		for (uint32_t slot = 0; slot < 3; slot++)
			params.setValue(nodeParamId(0, slot), 0.8);

		NodeParams next{ 0, { param("0/a", 0.1), param("0/other", 0.2), param("0/c", 0.3) } };
		next.params[0].maxValue = 0.5;
		std::vector<Id> gone = params.replaceNodeParams({ next }, { { "0/c", 0.25 } });

		CHECK(params.realValue(nodeParamId(0, 0)) == 0.5);	 // kept, clamped to the new range
		CHECK(params.realValue(nodeParamId(0, 1)) == 0.2);	 // another name at that id: its default
		CHECK(params.realValue(nodeParamId(0, 2)) == 0.25);	 // saved
		CHECK(gone == std::vector<Id>{ nodeParamId(0, 1) }); // "0/b" became "0/other"
	}

	TEST_CASE("params that go away leave their slots unused, and are reported gone")
	{
		ParamList params;
		params.replaceNodeParams({ node(0, { { "a", 0.1 }, { "b", 0.2 } }), node(2, { { "mix", 1.0 } }) }, {});

		std::vector<Id> gone = params.replaceNodeParams({ node(2, { { "mix", 1.0 } }) }, {});

		CHECK(gone == std::vector<Id>{ nodeParamId(0, 0), nodeParamId(0, 1) });
		CHECK_FALSE(params.used(nodeParamId(0, 0)));
		CHECK(params.at(nodeParamId(0, 0)).name.empty());
		CHECK(params.list().size() == 2);
	}

	TEST_CASE("at most 40 params per node; every node fits")
	{
		ParamList params;
		std::vector<NodeParams> nodes;
		for (uint32_t uid = 0; uid < kMaxNodes; uid++)
		{
			std::vector<std::pair<std::string, double>> members;
			for (size_t i = 0; i < kNodeSlots + 10; i++)
				members.emplace_back("p" + std::to_string(i), 0.5);
			nodes.push_back(node(uid, members));
		}
		params.replaceNodeParams(nodes, {});

		CHECK(params.list().size() == kParamCount);
		CHECK(params.at(nodeParamId(0, kNodeSlots - 1)).name == "0/p39");
		CHECK(params.at(nodeParamId(1, 0)).name == "1/p0"); // node 0's extra params were dropped
		CHECK(params.at(kParamCount - 1).name == "15/p39");
	}

	TEST_CASE("values are the host's, 0..1 over each param's range; real values in and out by name")
	{
		ParamList params;
		NodeParams pixelate{ 0, { param("0/blockSize", 16, 1, 128) } };
		params.replaceNodeParams({ pixelate }, {});
		const Id blockSize = nodeParamId(0, 0);

		CHECK(params.value(blockSize) == doctest::Approx(15.0 / 127)); // the default, as a host value
		CHECK(params.realValue(blockSize) == doctest::Approx(16));

		params.setValue(blockSize, 0.5); // the host
		CHECK(params.realValue(blockSize) == doctest::Approx(64.5));
		CHECK(params.valuesToJson()["0/blockSize"] == doctest::Approx(64.5));
		CHECK(params.toJson()[1]["value"] == doctest::Approx(64.5));

		params.setRealValue(blockSize, 1000); // the web UI: clamped
		CHECK(params.value(blockSize) == 1.0);
		params.valuesFromJson({ { "0/blockSize", 1 } });
		CHECK(params.value(blockSize) == 0.0);

		// replaced with another range: the real value is kept, within the new range
		params.setRealValue(blockSize, 100);
		params.replaceNodeParams({ { 0, { param("0/blockSize", 16, 1, 64) } } }, {});
		CHECK(params.realValue(blockSize) == doctest::Approx(64));
	}

	TEST_CASE("values go to and from JSON by name, used params only; unknown names and non-numbers are ignored")
	{
		ParamList params;
		params.replaceNodeParams({ node(0, { { "amount", 0.5 } }) }, {});
		params.setValue(AudioGain, 0.25);

		nlohmann::json saved = params.valuesToJson();
		CHECK(saved == nlohmann::json{ { "Audio Gain", 0.25 }, { "0/amount", 0.5 } });

		params.valuesFromJson({ { "0/amount", 0.75 }, { "Audio Gain", "loud" }, { "missing", 1 }, { "", 0.1 } });
		CHECK(params.value(nodeParamId(0, 0)) == 0.75);
		CHECK(params.value(AudioGain) == 0.25);
		CHECK(params.value(nodeParamId(0, 1)) == 0.5); // an unused slot isn't touched by the "" name
	}

	TEST_CASE("toJson describes the used params for the web UI, in id order, with their nodes")
	{
		ParamList params;
		params.replaceNodeParams({ node(2, { { "amount", 0.5 } }) }, {});

		nlohmann::json list = params.toJson();
		REQUIRE(list.size() == 2);
		CHECK(list[0]["id"] == AudioGain);
		CHECK(list[0]["name"] == "Audio Gain");
		CHECK(list[0]["group"] == "main");
		CHECK(list[0]["node"].is_null());
		CHECK(list[1]["id"] == nodeParamId(2, 0));
		CHECK(list[1]["group"] == "node");
		CHECK(list[1]["node"] == 2);
		CHECK(list[1]["value"] == 0.5);
		for (const char* key : { "label", "units", "defaultValue", "minValue", "maxValue" })
		{
			INFO(key);
			CHECK(list[1].contains(key));
		}
	}

	TEST_CASE("each flagged param is taken once for the host, by id, with its latest value; unused ones never")
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
		params.flagForHost(nodeParamId(5, 0)); // an unused slot: ignored
		params.flagForHost(kParamCount);	   // past the end: ignored

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
