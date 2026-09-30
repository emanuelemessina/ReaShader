/**
 * @file
 * @brief Host tests: the built plugin's loading, CLAP lifecycle, audio and video passthrough.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/host_helpers.h"

#include <doctest/doctest.h>

#include <string>

TEST_SUITE("host")
{
	TEST_CASE("the plugin loads and describes itself")
	{
		host::Reaper reaper(host::builtPlugin());

		REQUIRE(reaper.pluginCount() == 1);
		const clap_plugin_descriptor_t* descriptor = reaper.descriptor();
		REQUIRE(descriptor != nullptr);
#ifdef NDEBUG
		CHECK(std::string(descriptor->id) == "com.emanuelemessina.reashader");
		CHECK(std::string(descriptor->name) == "ReaShader");
#else
		CHECK(std::string(descriptor->id) == "com.emanuelemessina.reashader.debug");
		CHECK(std::string(descriptor->name) == "ReaShader (Debug)");
#endif
		test::checkNoProblems(reaper);
	}

	TEST_CASE("a new plugin lists only its fixed params, Audio Gain and LUT Mix, at 1")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();

		REQUIRE(reaper.params().size() == 2);
		CHECK(reaper.params()[0].name == "Audio Gain");
		CHECK(reaper.params()[0].value == 1.0);
		CHECK(reaper.params()[1].name == "LUT Mix");
		CHECK(reaper.params()[1].value == 1.0);

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("the lifecycle, twice: activate, process, deactivate")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();

		for (int cycle = 0; cycle < 2; cycle++)
		{
			INFO("cycle ", cycle);
			reaper.activate();
			CHECK(reaper.hasVideoProcessor());

			std::vector<float> output = reaper.processAudio(4, 0.5f);
			CHECK(output.front() == doctest::Approx(0.5f)); // Audio Gain 1: audio unchanged
			CHECK(output.back() == doctest::Approx(0.5f));
			reaper.idle();

			reaper.deactivate();
			CHECK_FALSE(reaper.hasVideoProcessor()); // the plugin deleted it
		}

		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("with no shader, video frames pass through unchanged")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.activate();

		host::Frame input = test::gradientFrame(321, 17);
		for (int frame = 0; frame < 3; frame++)
		{
			INFO("frame ", frame);
			host::Reaper::VideoResult result = reaper.renderVideo(input, frame / 30.0);
			CHECK(result.passthrough);
			CHECK(result.frame.bytes == input.bytes);
		}

		reaper.idle();
		reaper.destroyPlugin();
		test::checkNoProblems(reaper);
	}

	TEST_CASE("destroying an active plugin deactivates it first")
	{
		host::Reaper reaper(host::builtPlugin());
		reaper.createPlugin();
		reaper.activate();
		reaper.destroyPlugin();

		CHECK_FALSE(reaper.hasVideoProcessor());
		test::checkNoProblems(reaper);
	}
}
