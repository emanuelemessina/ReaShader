/**
 * @file
 * @brief Host tests: project scenarios (a chain via state, video-time values, state round trip, logo, v3 projects).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/host_helpers.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

TEST_SUITE("host")
{
	TEST_CASE("the param list: every id from the start, unused node slots hidden with no name")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();

		REQUIRE(reaper.params().size() == 1 + 16 * 40);
		for (size_t i = 0; i < reaper.params().size(); i++)
		{
			INFO(i);
			CHECK(reaper.params()[i].id == i); // index = id
		}
		CHECK_FALSE(reaper.params()[0].hidden);
		CHECK(reaper.params()[1].hidden);
		CHECK(reaper.params()[1].name.empty());

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("a project opened as REAPER opens it: activate, then the state; its params at once, no restart")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.activate();
		host::Frame input = test::gradientFrame(321, 17);

		reaper.loadState(test::projectState({ test::shaderNode(0, "brightness.frag") }));

		// observed: REAPER binds the project's envelopes right after the state: the params must be there now
		CHECK(reaper.restarts() == 0);
		CHECK(reaper.rescans() == 1);
		REQUIRE(reaper.visibleParams().size() == 2);
		CHECK(reaper.visibleParams()[0].name == "Audio Gain");
		const host::Param* brightness = reaper.param("[A] brightness: Brightness");
		REQUIRE(brightness != nullptr);
		CHECK(brightness->id == 1); // node 0's first slot
		CHECK(brightness->defaultValue == 0);
		CHECK(brightness->minValue == 0);
		CHECK(brightness->maxValue == 1);

		reaper.automate(brightness->id, 0.4);
		host::Reaper::VideoResult result = reaper.renderVideo(input, 0);
		CHECK_FALSE(result.passthrough);
		CHECK(test::brightnessMismatches(input, result.frame, 0.4) == 0);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("an envelope on a node's param, bound before the project's state loads, drives it at once")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.activate();

		// REAPER binds the project's envelope to its id as it loads: the id exists, still unused (hidden)
		const clap_id brightness = 1 + 40; // node B's first slot
		REQUIRE(reaper.params()[brightness].hidden);
		reaper.automate(brightness, 0.3);

		reaper.loadState(test::projectState({ test::shaderNode(1, "brightness.frag") }));
		CHECK(reaper.param("[B] brightness: Brightness")->id == brightness);
		CHECK(reaper.param("[B] brightness: Brightness")->value == 0.3);

		host::Frame input = test::gradientFrame(64, 8);
		CHECK(test::brightnessMismatches(input, reaper.renderVideo(input, 0).frame, 0.3) == 0);
		CHECK(reaper.restarts() == 0);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("a shader loaded before activation needs no restart")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();

		reaper.loadState(test::projectState({ test::shaderNode(0, "brightness.frag") }));
		REQUIRE(reaper.visibleParams().size() == 2); // there as soon as the state is loaded
		reaper.idle();
		CHECK(reaper.restarts() == 0);
		CHECK(reaper.rescans() == 1);
		const host::Param* brightness = reaper.param("[A] brightness: Brightness");
		REQUIRE(brightness != nullptr);

		reaper.activate(); // the renderer installs the kept chain in init()
		reaper.automate(brightness->id, 1.0);
		host::Frame input = test::gradientFrame(64, 8);
		host::Reaper::VideoResult result = reaper.renderVideo(input, 0);
		CHECK(test::brightnessMismatches(input, result.frame, 1.0) == 0);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("frames use the param values REAPER passes at video time, not the plugin's current ones")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.loadState(test::projectState({ test::shaderNode(0, "brightness.frag") }));
		reaper.idle();
		reaper.activate();
		const host::Param* brightness = reaper.param("[A] brightness: Brightness");
		REQUIRE(brightness != nullptr);
		clap_id id = brightness->id;
		host::Frame input = test::gradientFrame(64, 8);

		// automation not yet delivered through process(): the plugin still holds the default...
		reaper.automate(id, 0.8);
		CHECK(reaper.pluginValue(id) == 0);
		// ...but the frame gets the value at video time
		CHECK(test::brightnessMismatches(input, reaper.renderVideo(input, 0).frame, 0.8) == 0);

		// the next audio block delivers it
		reaper.processAudio(1, 0.0f);
		CHECK(reaper.pluginValue(id) == doctest::Approx(0.8));

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("a chain from a project: nodes in order, each with its own params, bypassed ones left out")
	{
		auto invert = [](double v) { return 255 - v; };
		ReaShader::gpu::LutData inverted = test::invertLut();

		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.loadState(
			test::projectState({ test::lutNode(2, "invert", inverted), test::shaderNode(0, "brightness.frag"),
								 test::lutNode(5, "invert", inverted, true), test::shaderNode(1, "brightness.frag") },
							   { { "0/brightness", 0.2 }, { "1/brightness", 0.1 } }));
		reaper.idle();
		reaper.activate();

		// in id order (node uid order), after Audio Gain; the bypassed node's param is there too
		std::vector<host::Param> visible = reaper.visibleParams();
		REQUIRE(visible.size() == 5);
		CHECK(visible[1].id == 1);
		CHECK(visible[2].id == 1 + 40);
		CHECK(visible[3].id == 1 + 2 * 40);
		CHECK(visible[4].id == 1 + 5 * 40);

		host::Frame input = test::gradientFrame(64, 8);
		host::Reaper::VideoResult result = reaper.renderVideo(input, 0);
		CHECK_FALSE(result.passthrough);
		CHECK(test::mismatches(input, result.frame, [&](double v) {
				  return test::brighten(std::min(255.0, test::brighten(invert(v), 0.2)), 0.1);
			  }) == 0);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("automation stays with its node when a project reorders the chain")
	{
		auto invert = [](double v) { return 255 - v; };
		ReaShader::gpu::LutData inverted = test::invertLut();

		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.loadState(
			test::projectState({ test::shaderNode(0, "brightness.frag"), test::lutNode(1, "invert", inverted) }));
		reaper.idle();
		reaper.activate();
		const clap_id brightness = reaper.param("[A] brightness: Brightness")->id;
		const clap_id mix = reaper.param("[B] invert: Mix")->id;

		// the same nodes, the other way round
		reaper.loadState(
			test::projectState({ test::lutNode(1, "invert", inverted), test::shaderNode(0, "brightness.frag") }));
		reaper.idle();
		// the labels and the ids stay
		CHECK(reaper.param("[A] brightness: Brightness")->id == brightness);
		CHECK(reaper.param("[B] invert: Mix")->id == mix);
		CHECK(reaper.restarts() == 0);

		reaper.automate(brightness, 0.2);
		reaper.automate(mix, 1.0);
		host::Frame input = test::gradientFrame(64, 8);
		CHECK(test::mismatches(input, reaper.renderVideo(input, 0).frame,
							   [&](double v) { return test::brighten(invert(v), 0.2); }) == 0);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("the state round-trips: the chain, param values, logo")
	{
		std::string saved;
		{
			host::Reaper reaper(host::builtPlugin());
			reaper.createPlugin();
			reaper.activate();
			reaper.loadState(test::projectState(
				{ test::lutNode(1, "invert", test::invertLut()), test::shaderNode(0, "brightness.frag", true) }, {},
				true));
			reaper.idle();

			const host::Param* brightness = reaper.param("[A] brightness: Brightness");
			const host::Param* gain = reaper.param("Audio Gain");
			REQUIRE(brightness != nullptr);
			REQUIRE(gain != nullptr);
			reaper.automate(brightness->id, 0.7);
			reaper.automate(gain->id, 0.5);
			std::vector<float> audio = reaper.processAudio(1, 0.5f);
			CHECK(audio.front() == doctest::Approx(0.25f)); // gain 0.5 applies

			saved = reaper.saveState();
			reaper.destroyPlugin();
			test::checkNoProblems(reaper);
		}

		// a new instance: the project reopened
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.loadState(saved);
		reaper.idle();

		const host::Param* brightness = reaper.param("[A] brightness: Brightness");
		const host::Param* gain = reaper.param("Audio Gain");
		REQUIRE(brightness != nullptr);
		REQUIRE(gain != nullptr);
		CHECK(brightness->value == doctest::Approx(0.7)); // restored by name once the nodes' params exist
		CHECK(gain->value == doctest::Approx(0.5));

		json first = json::parse(saved);
		json second = json::parse(reaper.saveState());
		CHECK(second == first);
		CHECK(first["version"] == 4);
		CHECK(first["logo"] == true);
		REQUIRE(first["chain"].size() == 2);
		CHECK(first["chain"][0]["name"] == "invert");
		CHECK(first["chain"][1]["name"] == "brightness");
		CHECK(first["chain"][1]["bypass"] == true);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("the logo from a project draws over otherwise unchanged video")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.activate();
		reaper.loadState(test::projectState(json::array(), {}, true));
		reaper.idle();

		host::Frame input = test::solidFrame(640, 360, 10, 20, 30);
		host::Reaper::VideoResult result = reaper.renderVideo(input, 1.0);
		CHECK_FALSE(result.passthrough);

		int changed = 0;
		for (int y = 0; y < 360; y++)
			for (int x = 0; x < 640; x++)
			{
				const uint8_t* p = result.frame.at(x, y);
				changed += p[0] != 10 || p[1] != 20 || p[2] != 30;
			}
		CHECK(changed > 1000);			// the logo is there...
		CHECK(changed < 640 * 360 / 2); // ...in the middle
		const uint8_t* corner = result.frame.at(0, 0);
		CHECK(corner[0] == 10);
		CHECK(corner[1] == 20);
		CHECK(corner[2] == 30);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("a version 3 project: the LUT before or after the shader, or left to it; its Mix blends")
	{
		auto invert = [](double v) { return 255 - v; };
		auto brighten = [](double v) { return test::brighten(v, 0.2); };
		struct Case
		{
			const char* shader;
			const char* mode;
			std::function<double(double)> expected;
		};
		const Case cases[] = {
			{ "brightness.frag", "before", [&](double v) { return brighten(invert(v)); } },
			{ "brightness.frag", "after", [&](double v) { return invert(std::min(255.0, brighten(v))); } },
			{ "brightness.frag", "shader", brighten }, // brightness.frag doesn't sample iChannel1
			{ "", "shader", invert },				   // no shader: the LUT alone
		};

		host::Frame input = test::gradientFrame(64, 8);
		for (const Case& c : cases)
		{
			INFO("shader '", c.shader, "', mode ", c.mode);
			host::Reaper reaper(host::builtPlugin());
			reaper.createPlugin();
			reaper.loadState(test::v3ProjectState(c.shader, false, { { "brightness", 0.2 } },
												  test::v3ProjectLut("invert", test::invertLut(), c.mode)));
			reaper.idle();
			reaper.activate();

			host::Reaper::VideoResult result = reaper.renderVideo(input, 0);
			CHECK_FALSE(result.passthrough);
			CHECK(test::mismatches(input, result.frame, c.expected) == 0);

			// the LUT node's Mix at 0: the frame as if there were no LUT; 0.5: halfway
			if (std::string(c.mode) == "after")
			{
				const host::Param* mix = reaper.param("[B] invert: Mix");
				REQUIRE(mix != nullptr);
				CHECK(mix->id == 1 + 40); // node 1's first slot
				reaper.automate(mix->id, 0.0);
				CHECK(test::brightnessMismatches(input, reaper.renderVideo(input, 0).frame, 0.2) == 0);
				reaper.automate(mix->id, 0.5);
				CHECK(test::mismatches(input, reaper.renderVideo(input, 0).frame, [&](double v) {
						  double brightened = std::min(255.0, brighten(v));
						  return (brightened + invert(brightened)) / 2;
					  }) == 0);
			}

			reaper.destroyPlugin();
			test::checkNoProblems(reaper);
		}
	}

	TEST_CASE("the host sees every param as 0..1 over its range, and its real value as text")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.loadState(test::projectState({ test::shaderNode(0, "pixelate.frag") }, { { "0/blockSize", 64.5 } }));
		reaper.activate();

		// pixelate.frag: //@param blockSize 'Block size (px)' 16 1 128
		const host::Param* blockSize = reaper.param("[A] pixelate: Block size (px)");
		REQUIRE(blockSize != nullptr);
		CHECK(blockSize->minValue == 0);
		CHECK(blockSize->maxValue == 1);
		CHECK(blockSize->value == doctest::Approx(0.5)); // (64.5 - 1) / 127
		CHECK(reaper.paramText(blockSize->id, 0.5) == "64.500");
		CHECK(reaper.paramFromText(blockSize->id, "128") == doctest::Approx(1.0));

		// automation at 1.0 is the top of the range
		reaper.automate(blockSize->id, 1.0);
		reaper.processAudio(1, 0.0f);
		CHECK(nlohmann::json::parse(reaper.saveState())["params"]["0/blockSize"] == doctest::Approx(128.0));

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("an unrecognized state keeps the defaults")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.loadState("not a ReaShader state");
		reaper.idle();

		REQUIRE(reaper.visibleParams().size() == 1);
		CHECK(reaper.visibleParams()[0].name == "Audio Gain");

		reaper.activate();
		host::Frame input = test::gradientFrame(32, 4);
		CHECK(reaper.renderVideo(input, 0).passthrough);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}
}
