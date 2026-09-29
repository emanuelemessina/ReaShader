/**
 * @file
 * @brief Unit tests: the web UI protocol, on a plugin that is never activated (no GPU).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

// openUrl isn't tested: a valid URL would open the browser.

#include "support/support.h"

#include "plugin/plugin.h"
#include "util/paths.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace ReaShader;
using nlohmann::json;

namespace
{
	// A plugin with the web UI's side of the protocol: messages in through handleWebUIMessage(),
	// messages out recorded. The host only counts what the plugin asks of it.
	struct UiSession
	{
		UiSession()
		{
			// uploads are stored here: start from none
			std::filesystem::remove_all(util::paths::compiledShadersDir());

			plugin.initialize(&clapHost);
			plugin.setWebUISender([this](const std::string& message) { sent.push_back(json::parse(message)); });
		}

		~UiSession()
		{
			plugin.clearWebUISender();
		}

		void receive(const json& message)
		{
			plugin.handleWebUIMessage(message.dump());
		}

		// the last message of that type the plugin sent, null if none
		json last(const std::string& type) const
		{
			for (auto it = sent.rbegin(); it != sent.rend(); ++it)
				if ((*it)["type"] == type)
					return *it;
			return nullptr;
		}

		json savedState()
		{
			struct Out
			{
				clap_ostream_t stream{ this, write };
				std::string data;
				static int64_t write(const clap_ostream_t* s, const void* buffer, uint64_t size)
				{
					static_cast<Out*>(s->ctx)->data.append(static_cast<const char*>(buffer), (size_t)size);
					return (int64_t)size;
				}
			} out;
			REQUIRE(plugin.saveState(&out.stream));
			return json::parse(out.data);
		}

		int callbacks = 0, restarts = 0, rescans = 0, flushes = 0;
		std::vector<json> sent;
		ReaShaderPlugin plugin;

	  private:
		static UiSession* self(const clap_host_t* host)
		{
			return static_cast<UiSession*>(host->host_data);
		}
		static const void* getExtension(const clap_host_t*, const char* id)
		{
			static const clap_host_params_t hostParams{
				[](const clap_host_t* host, clap_param_rescan_flags) { self(host)->rescans++; },
				[](const clap_host_t*, clap_id, clap_param_clear_flags) {},
				[](const clap_host_t* host) { self(host)->flushes++; },
			};
			return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &hostParams : nullptr;
		}

		clap_host_t clapHost{ CLAP_VERSION_INIT,
							  this,
							  "stub",
							  "",
							  "",
							  "0",
							  getExtension,
							  [](const clap_host_t* host) { self(host)->restarts++; },
							  [](const clap_host_t*) {},
							  [](const clap_host_t* host) { self(host)->callbacks++; } };
	};

	std::string example(const char* file)
	{
		return test::readFile(test::repoPath("src/shaders/examples") / file);
	}
} // namespace

TEST_SUITE("protocol")
{
	TEST_CASE("ready: a snapshot of everything the UI shows")
	{
		UiSession ui;
		ui.receive({ { "type", "ready" } });

		json snapshot = ui.last("snapshot");
		REQUIRE(snapshot.is_object());
		CHECK_FALSE(snapshot["version"].get<std::string>().empty());
		REQUIRE(snapshot["params"].size() == 1);
		CHECK(snapshot["params"][0]["name"] == "Audio Gain");
		CHECK(snapshot["logo"] == false);
		CHECK(snapshot["shader"]["name"] == "");
		CHECK(snapshot["shaders"] == json::array());
		CHECK(snapshot["devices"]["selected"] == 0);
		CHECK(snapshot.contains("track"));
	}

	TEST_CASE("paramValue: the value is set, and passed to the host once, through a flush request")
	{
		UiSession ui;
		ui.receive({ { "type", "paramValue" }, { "id", 0 }, { "value", 0.3 } });

		double value = 0;
		REQUIRE(ui.plugin.getParamValue(0, &value));
		CHECK(value == 0.3);
		CHECK(ui.flushes == 1);

		clap_id id = 99;
		REQUIRE(ui.plugin.takeParamChangeForHost(id, value));
		CHECK(id == 0);
		CHECK(value == 0.3);
		CHECK_FALSE(ui.plugin.takeParamChangeForHost(id, value));
	}

	TEST_CASE("paramValue for a param that doesn't exist is ignored")
	{
		UiSession ui;
		ui.receive({ { "type", "paramValue" }, { "id", 42 }, { "value", 0.3 } });

		CHECK(ui.flushes == 0);
		clap_id id;
		double value;
		CHECK_FALSE(ui.plugin.takeParamChangeForHost(id, value));
	}

	TEST_CASE("host automation is echoed to the UI on the main thread")
	{
		UiSession ui;
		ui.plugin.applyHostParamValue(0, 0.25); // audio thread
		CHECK(ui.callbacks == 1);
		CHECK(ui.last("paramValue").is_null());

		ui.plugin.onMainThread();
		json echo = ui.last("paramValue");
		REQUIRE(echo.is_object());
		CHECK(echo["id"] == 0);
		CHECK(echo["value"] == 0.25);
	}

	TEST_CASE("shaderUpload: compiled, stored, loaded; its params reach the host on the main thread")
	{
		UiSession ui;
		ui.receive({ { "type", "shaderUpload" }, { "name", "brightness.frag" }, { "source", example("brightness.frag") } });

		json status = ui.last("shaderStatus");
		REQUIRE(status.is_object());
		CHECK(status["state"] == "ok");
		CHECK(status["status"] == "Loaded brightness");
		CHECK(std::filesystem::exists(util::paths::compiledShadersDir() / "brightness.json"));
		CHECK(ui.callbacks >= 1);

		// inactive: the new params are applied right away, and the host rescans
		ui.plugin.onMainThread();
		CHECK(ui.rescans == 1);
		CHECK(ui.restarts == 0);
		CHECK(ui.plugin.automatableParamCount() == 2);

		json snapshot = ui.last("snapshot");
		CHECK(snapshot["shader"]["name"] == "brightness");
		CHECK(snapshot["shaders"] == json::array({ "brightness" }));
		REQUIRE(snapshot["params"].size() == 2);
		CHECK(snapshot["params"][1]["label"] == "Brightness");
	}

	TEST_CASE("a broken upload reports the error and keeps the current shader")
	{
		UiSession ui;
		ui.receive({ { "type", "shaderUpload" }, { "name", "brightness.frag" }, { "source", example("brightness.frag") } });
		ui.receive({ { "type", "shaderUpload" },
					 { "name", "broken.frag" },
					 { "source", test::readFile(test::repoPath("test/shaders/broken.frag")) } });

		json status = ui.last("shaderStatus");
		CHECK(status["state"] == "error");
		CHECK(status["status"].get<std::string>().find("broken.frag:7") != std::string::npos);
		CHECK_FALSE(std::filesystem::exists(util::paths::compiledShadersDir() / "broken.json"));

		ui.receive({ { "type", "ready" } });
		CHECK(ui.last("snapshot")["shader"]["name"] == "brightness");
	}

	TEST_CASE("shaderSelect: a stored shader by name, \"\" for none, never a path")
	{
		UiSession ui;
		ui.receive({ { "type", "shaderUpload" }, { "name", "tint.frag" }, { "source", example("tint.frag") } });

		ui.receive({ { "type", "shaderSelect" }, { "name", "" } });
		CHECK(ui.last("shaderStatus")["status"] == "No shader: video passes through");
		CHECK(ui.last("snapshot")["shader"]["name"] == "");

		ui.receive({ { "type", "shaderSelect" }, { "name", "tint" } });
		CHECK(ui.last("shaderStatus")["status"] == "Loaded tint");
		CHECK(ui.last("snapshot")["shader"]["name"] == "tint");

		// only the file name counts: "../tint" is "tint", "../../missing" is "missing"
		ui.receive({ { "type", "shaderSelect" }, { "name", "../../missing" } });
		CHECK(ui.last("shaderStatus")["state"] == "error");
		CHECK(ui.last("shaderStatus")["status"] == "Can't read the compiled shader missing");
	}

	TEST_CASE("logo and renderingDevice are saved with the project")
	{
		UiSession ui;
		ui.receive({ { "type", "logo" }, { "enabled", true } });
		ui.receive({ { "type", "renderingDevice" }, { "index", 1 } });

		CHECK(ui.last("snapshot")["devices"]["selected"] == 1);
		json state = ui.savedState();
		CHECK(state["logo"] == true);
		CHECK(state["device"] == 1);

		ui.receive({ { "type", "logo" }, { "enabled", false } });
		CHECK(ui.savedState()["logo"] == false);
	}

	TEST_CASE("unknown and malformed messages are ignored")
	{
		UiSession ui;
		ui.plugin.handleWebUIMessage("not json");
		ui.plugin.handleWebUIMessage("[1, 2]");
		ui.receive({ { "type", "noSuchMessage" } });
		ui.receive({ { "value", 1 } });

		CHECK(ui.sent.empty());
		CHECK(ui.callbacks == 0);
	}
}
