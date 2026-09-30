/**
 * @file
 * @brief Host tests: project scenarios (shader via state, video-time values, state round trip, logo).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/host_helpers.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

TEST_SUITE("host")
{
	TEST_CASE("a shader loaded with a project while active: restart, rescan, then its params and frames")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.activate();
		host::Frame input = test::gradientFrame(321, 17);

		reaper.loadState(test::projectState("brightness.frag", false));

		// before the restart, frames already go through the shader, its params at their defaults (0)
		host::Reaper::VideoResult pending = reaper.renderVideo(input, 0);
		CHECK_FALSE(pending.passthrough);
		CHECK(test::brightnessMismatches(input, pending.frame, 0) == 0);

		reaper.idle(); // on_main_thread -> request_restart -> deactivate (rescan) -> activate
		CHECK(reaper.restarts() == 1);
		CHECK(reaper.rescans() == 1);
		CHECK(reaper.isActive());

		REQUIRE(reaper.params().size() == 3);
		CHECK(reaper.params()[0].name == "Audio Gain");
		CHECK(reaper.params()[1].name == "LUT Mix");
		const host::Param* brightness = reaper.param("Brightness");
		REQUIRE(brightness != nullptr);
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

	TEST_CASE("a shader loaded before activation needs no restart")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();

		reaper.loadState(test::projectState("brightness.frag", false));
		reaper.idle(); // inactive: the params are applied right away
		CHECK(reaper.restarts() == 0);
		CHECK(reaper.rescans() == 1);
		const host::Param* brightness = reaper.param("Brightness");
		REQUIRE(brightness != nullptr);

		reaper.activate(); // the renderer installs the kept shader in init()
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
		reaper.loadState(test::projectState("brightness.frag", false));
		reaper.idle();
		reaper.activate();
		const host::Param* brightness = reaper.param("Brightness");
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

	TEST_CASE("the state round-trips: shader, LUT, param values, logo")
	{
		std::string saved;
		{
			host::Reaper reaper(host::builtPlugin());
			reaper.createPlugin();
			reaper.activate();
			reaper.loadState(test::projectState("brightness.frag", true, nlohmann::json::object(),
												test::projectLut("invert", test::invertLut(), "before")));
			reaper.idle();

			const host::Param* brightness = reaper.param("Brightness");
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

		const host::Param* brightness = reaper.param("Brightness");
		const host::Param* gain = reaper.param("Audio Gain");
		REQUIRE(brightness != nullptr);
		REQUIRE(gain != nullptr);
		CHECK(brightness->value == doctest::Approx(0.7)); // restored by name once the shader's params exist
		CHECK(gain->value == doctest::Approx(0.5));

		nlohmann::json first = nlohmann::json::parse(saved);
		nlohmann::json second = nlohmann::json::parse(reaper.saveState());
		CHECK(second == first);
		CHECK(first["logo"] == true);
		CHECK(first["shader"]["name"] == "brightness");
		CHECK(first["lut"]["name"] == "invert");
		CHECK(first["lut"]["mode"] == "before");

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("the logo from a project draws over otherwise unchanged video")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.activate();
		reaper.loadState(test::projectState("", true));
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

	TEST_CASE("a LUT from a project: before or after the shader, or left to it; LUT Mix blends")
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
			{ "", "shader", invert },				   // no shader: the LUT pass runs alone
		};

		host::Frame input = test::gradientFrame(64, 8);
		for (const Case& c : cases)
		{
			INFO("shader '", c.shader, "', mode ", c.mode);
			host::Reaper reaper(host::builtPlugin());
			reaper.createPlugin();
			reaper.loadState(test::projectState(c.shader, false, { { "brightness", 0.2 } },
												test::projectLut("invert", test::invertLut(), c.mode)));
			reaper.idle();
			reaper.activate();

			host::Reaper::VideoResult result = reaper.renderVideo(input, 0);
			CHECK_FALSE(result.passthrough);
			CHECK(test::mismatches(input, result.frame, c.expected) == 0);

			// LUT Mix 0: the frame as if there were no LUT; 0.5: halfway
			const host::Param* mix = reaper.param("LUT Mix");
			REQUIRE(mix != nullptr);
			if (std::string(c.mode) == "after")
			{
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

	TEST_CASE("an unrecognized state keeps the defaults")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.loadState("not a ReaShader state");
		reaper.idle();

		REQUIRE(reaper.params().size() == 2);
		CHECK(reaper.params()[0].name == "Audio Gain");
		CHECK(reaper.params()[1].name == "LUT Mix");

		reaper.activate();
		host::Frame input = test::gradientFrame(32, 4);
		CHECK(reaper.renderVideo(input, 0).passthrough);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}
}
