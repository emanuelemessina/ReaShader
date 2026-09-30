/**
 * @file
 * @brief Unit tests: the web UI protocol and the state, on a plugin that is never activated (no GPU).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

// openUrl isn't tested: a valid URL would open the browser.

#include "support/support.h"

#include "plugin/plugin.h"
#include "render/lut_file.h"
#include "render/shader_compiler.h"
#include "util/paths.h"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace ReaShader;
using nlohmann::json;
using Parameters::nodeParamId;

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
			std::filesystem::remove_all(util::paths::lutsDir());

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

		// receives a message, then lets the plugin apply new params (inactive: right away)
		void edit(const json& message)
		{
			receive(message);
			plugin.onMainThread();
		}

		// the last message of that type the plugin sent, null if none
		json last(const std::string& type) const
		{
			for (auto it = sent.rbegin(); it != sent.rend(); ++it)
				if ((*it)["type"] == type)
					return *it;
			return nullptr;
		}

		// the chain as the UI sees it, one "<uid>:<kind>:<name>" per node
		std::vector<std::string> chain()
		{
			receive({ { "type", "ready" } });
			std::vector<std::string> nodes;
			json snapshot = last("snapshot");
			for (const json& node : snapshot["chain"])
				nodes.push_back(std::to_string(node["uid"].get<int>()) + ":" + node["kind"].get<std::string>() + ":" +
								node["name"].get<std::string>());
			return nodes;
		}

		// the snapshot's param with that label, null if none
		json param(const std::string& label)
		{
			receive({ { "type", "ready" } });
			json snapshot = last("snapshot");
			for (const json& p : snapshot["params"])
				if (p["label"] == label)
					return p;
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

		// loads a state; inactive, the plugin applies its params right away
		void loadState(const json& state)
		{
			struct In
			{
				clap_istream_t stream{ this, read };
				std::string data;
				size_t at = 0;
				static int64_t read(const clap_istream_t* s, void* buffer, uint64_t size)
				{
					In* in = static_cast<In*>(s->ctx);
					size_t n = std::min((size_t)size, in->data.size() - in->at);
					std::memcpy(buffer, in->data.data() + in->at, n);
					in->at += n;
					return (int64_t)n;
				}
			} in;
			in.data = state.dump();
			REQUIRE(plugin.loadState(&in.stream));
		}

		double value(clap_id id)
		{
			double result = -1;
			CHECK(plugin.getParamValue(id, &result));
			return result;
		}

		int callbacks = 0, restarts = 0, rescans = 0, flushes = 0;
		std::vector<clap_id> cleared; // params the plugin asked the host to forget
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
				[](const clap_host_t* host, clap_id id, clap_param_clear_flags) { self(host)->cleared.push_back(id); },
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

	std::string lutFixture(const char* file)
	{
		return test::readFile(test::repoPath("test/luts") / file);
	}

	void uploadShader(UiSession& ui, const char* file)
	{
		ui.edit({ { "type", "shaderUpload" }, { "name", file }, { "source", example(file) } });
	}

	void uploadLut(UiSession& ui, const char* file)
	{
		ui.edit({ { "type", "lutUpload" }, { "name", file }, { "source", lutFixture(file) } });
	}

	// a shader with `vectors` vec4 sliders (4 each)
	std::string manySliders(int vectors)
	{
		std::string members, sum = "vec4(0)";
		for (int i = 0; i < vectors; i++)
		{
			members += "vec4 p" + std::to_string(i) + "; ";
			sum += " + p" + std::to_string(i);
		}
		return "uniform Params { " + members + "};\nvoid main() { fragColor = " + sum + "; }\n";
	}

	std::string status(UiSession& ui)
	{
		return ui.last("chainStatus")["status"];
	}

	// a version 3 state: one shader (an example, compiled here), one LUT (inverting), a LUT mode
	json stateV3(const char* shader, bool withLut, const char* mode, json params)
	{
		json compiled;
		if (shader)
			compiled = gpu::toJson(gpu::compileShader(example(shader), shader));
		return { { "version", 3 },
				 { "params", params },
				 { "device", 0 },
				 { "logo", false },
				 { "shader", { { "name", shader ? "brightness" : "" }, { "compiled", compiled } } },
				 { "lut",
				   { { "name", withLut ? "invert" : "" },
					 { "mode", mode },
					 { "data", withLut ? gpu::toJson(test::invertLut()) : json() } } } };
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
		REQUIRE(snapshot["params"].size() == 1); // the used ones: Audio Gain
		CHECK(snapshot["params"][0]["name"] == "Audio Gain");
		CHECK(snapshot["params"][0]["id"] == Parameters::AudioGain);
		CHECK(snapshot["logo"] == false);
		CHECK(snapshot["chain"] == json::array());
		CHECK(snapshot["shaders"] == json::array());
		CHECK(snapshot["luts"] == json::array());
		CHECK(snapshot["devices"]["selected"] == 0);
		CHECK(snapshot.contains("track"));
	}

	TEST_CASE("paramValue: the value is set, and passed to the host once, through a flush request")
	{
		UiSession ui;
		ui.receive({ { "type", "paramValue" }, { "id", 0 }, { "value", 0.3 } });

		CHECK(ui.value(0) == 0.3);
		CHECK(ui.flushes == 1);

		clap_id id = 99;
		double value = 0;
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

		// every automatable param is echoed
		ui.plugin.onMainThread();
		json echo;
		for (const json& message : ui.sent)
			if (message["type"] == "paramValue" && message["id"] == Parameters::AudioGain)
				echo = message;
		REQUIRE(echo.is_object());
		CHECK(echo["value"] == 0.25);
	}

	TEST_CASE("shaderUpload: compiled, stored, appended as a node; its params reach the host on the main thread")
	{
		UiSession ui;
		ui.receive(
			{ { "type", "shaderUpload" }, { "name", "brightness.frag" }, { "source", example("brightness.frag") } });

		CHECK(ui.last("chainStatus")["state"] == "ok");
		CHECK(status(ui) == "Added brightness");
		CHECK(std::filesystem::exists(util::paths::compiledShadersDir() / "brightness.json"));
		CHECK(ui.callbacks >= 1);

		// the params are there right away; the host rescans their names and values on the main thread
		ui.plugin.onMainThread();
		CHECK(ui.rescans == 1);
		CHECK(ui.restarts == 0);
		CHECK(ui.plugin.paramCount() == Parameters::kParamCount); // the list never changes size

		json snapshot = ui.last("snapshot");
		CHECK(snapshot["chain"] == json::array({ { { "uid", 0 },
												   { "tag", "A" },
												   { "kind", "shader" },
												   { "name", "brightness" },
												   { "bypass", false },
												   { "lut", "" },
												   { "samplesLut", false } } }));
		CHECK(snapshot["shaders"] == json::array({ "brightness" }));
		REQUIRE(snapshot["params"].size() == 2);
		json brightness = snapshot["params"][1];
		CHECK(brightness["label"] == "[A] brightness: Brightness");
		CHECK(brightness["name"] == "0/brightness");
		CHECK(brightness["id"] == nodeParamId(0, 0));
		CHECK(brightness["node"] == 0);
	}

	TEST_CASE("a broken upload reports the error and leaves the chain as it was")
	{
		UiSession ui;
		uploadShader(ui, "brightness.frag");
		ui.edit({ { "type", "shaderUpload" },
				  { "name", "broken.frag" },
				  { "source", test::readFile(test::repoPath("test/shaders/broken.frag")) } });

		CHECK(ui.last("chainStatus")["state"] == "error");
		CHECK(status(ui).find("broken.frag:7") != std::string::npos);
		CHECK_FALSE(std::filesystem::exists(util::paths::compiledShadersDir() / "broken.json"));
		CHECK(ui.chain() == std::vector<std::string>{ "0:shader:brightness" });
	}

	TEST_CASE("lutUpload: parsed, stored, appended as a node with a Mix param, and saved with the project")
	{
		UiSession ui;
		uploadShader(ui, "brightness.frag");
		uploadLut(ui, "invert.cube");

		CHECK(status(ui) == "Added invert");
		CHECK(std::filesystem::exists(util::paths::lutsDir() / "invert.json"));
		CHECK(ui.chain() == std::vector<std::string>{ "0:shader:brightness", "1:lut:invert" });
		CHECK(ui.last("snapshot")["luts"] == json::array({ "invert" }));

		json mix = ui.param("[B] invert: Mix");
		REQUIRE(mix.is_object());
		CHECK(mix["id"] == nodeParamId(1, 0));
		CHECK(mix["name"] == "1/mix");
		CHECK(mix["value"] == 1.0);

		json state = ui.savedState();
		CHECK(state["version"] == 4);
		REQUIRE(state["chain"].size() == 2);
		json node = state["chain"][1];
		CHECK(node["uid"] == 1);
		CHECK(node["kind"] == "lut");
		CHECK(node["name"] == "invert");
		CHECK_FALSE(node.contains("lut"));
		gpu::LutData stored = gpu::lutFromJson(node["data"]);
		CHECK(stored.size == 2);
		CHECK(stored.title == "Invert");
	}

	TEST_CASE("a broken .cube reports the error and leaves the chain as it was")
	{
		UiSession ui;
		uploadLut(ui, "invert.cube");
		uploadLut(ui, "broken.cube");

		CHECK(ui.last("chainStatus")["state"] == "error");
		CHECK(status(ui).find("broken.cube:6") != std::string::npos);
		CHECK_FALSE(std::filesystem::exists(util::paths::lutsDir() / "broken.json"));
		CHECK(ui.chain() == std::vector<std::string>{ "0:lut:invert" });
	}

	TEST_CASE("nodeAdd: a stored shader or LUT by name, at an index or last, never from a path")
	{
		UiSession ui;
		uploadShader(ui, "tint.frag");
		uploadLut(ui, "invert.cube");
		ui.edit({ { "type", "nodeRemove" }, { "uid", 0 } });
		ui.edit({ { "type", "nodeRemove" }, { "uid", 1 } });
		REQUIRE(ui.chain().empty());

		ui.edit({ { "type", "nodeAdd" }, { "kind", "lut" }, { "name", "invert" } });
		ui.edit({ { "type", "nodeAdd" }, { "kind", "shader" }, { "name", "tint" }, { "index", 0 } });
		ui.edit({ { "type", "nodeAdd" }, { "kind", "shader" }, { "name", "../../tint" }, { "index", 99 } });
		CHECK(status(ui) == "Added tint");
		// the smallest free uids: the removed ones come back at once
		CHECK(ui.chain() == std::vector<std::string>{ "1:shader:tint", "0:lut:invert", "2:shader:tint" });

		// the same shader twice: two nodes, each with its own params, told apart by their tag
		CHECK(ui.param("[B] tint: Amount")["id"] == nodeParamId(1, 0));
		CHECK(ui.param("[C] tint: Amount")["id"] == nodeParamId(2, 0));

		ui.edit({ { "type", "nodeAdd" }, { "kind", "shader" }, { "name", "missing" } });
		CHECK(ui.last("chainStatus")["state"] == "error");
		CHECK(status(ui) == "Can't read the stored shader missing");
		ui.edit({ { "type", "nodeAdd" }, { "kind", "mesh" }, { "name", "tint" } });
		CHECK(status(ui) == "Unknown node kind");
		ui.edit({ { "type", "nodeAdd" }, { "kind", "lut" }, { "name", "tint" } }); // not a LUT
		CHECK(ui.last("chainStatus")["state"] == "error");
		CHECK(ui.chain().size() == 3);
	}

	TEST_CASE("a shader with more than 40 sliders is rejected everywhere, never loaded in part")
	{
		UiSession ui;

		// upload: not even stored
		ui.edit({ { "type", "shaderUpload" }, { "name", "big.frag" }, { "source", manySliders(11) } });
		CHECK(ui.last("chainStatus")["state"] == "error");
		CHECK(status(ui) == "big has 44 sliders, and a shader can have at most 40");
		CHECK_FALSE(std::filesystem::exists(util::paths::compiledShadersDir() / "big.json"));
		CHECK(ui.chain().empty());

		// exactly 40: fine
		ui.edit({ { "type", "shaderUpload" }, { "name", "forty.frag" }, { "source", manySliders(10) } });
		CHECK(status(ui) == "Added forty");
		CHECK(ui.chain() == std::vector<std::string>{ "0:shader:forty" });

		// a stored one from elsewhere (e.g. an older version): not added, not swapped in
		json big = gpu::toJson(gpu::compileShader(manySliders(11), "big.frag"));
		std::ofstream(util::paths::compiledShadersDir() / "big.json") << big.dump();
		ui.edit({ { "type", "nodeAdd" }, { "kind", "shader" }, { "name", "big" } });
		CHECK(status(ui) == "big has 44 sliders, and a shader can have at most 40");
		ui.edit({ { "type", "nodeSet" }, { "uid", 0 }, { "name", "big" } });
		CHECK(status(ui) == "big has 44 sliders, and a shader can have at most 40");
		CHECK(ui.chain() == std::vector<std::string>{ "0:shader:forty" });

		// in a project: left out of the chain, and the UI told
		UiSession other;
		other.loadState(
			{ { "version", 4 },
			  { "params", json::object() },
			  { "chain", json::array({ { { "uid", 0 }, { "kind", "shader" }, { "name", "big" }, { "data", big } },
									   { { "uid", 1 },
										 { "kind", "lut" },
										 { "name", "invert" },
										 { "data", gpu::toJson(test::invertLut()) } } }) } });
		CHECK(other.chain() == std::vector<std::string>{ "1:lut:invert" });
		CHECK(other.last("chainStatus")["state"] == "error");
		CHECK(status(other).find("big has 44 sliders") != std::string::npos);
	}

	TEST_CASE("a chain holds at most 16 nodes; an upload to a full chain is still stored")
	{
		UiSession ui;
		uploadLut(ui, "invert.cube");
		for (int i = 1; i < 16; i++)
			ui.receive({ { "type", "nodeAdd" }, { "kind", "lut" }, { "name", "invert" } });
		ui.plugin.onMainThread();
		CHECK(ui.chain().size() == 16);

		ui.edit({ { "type", "nodeAdd" }, { "kind", "lut" }, { "name", "invert" } });
		CHECK(status(ui) == "The chain is full (16 nodes)");
		uploadShader(ui, "tint.frag");
		CHECK(status(ui) == "The chain is full (16 nodes)");
		CHECK(std::filesystem::exists(util::paths::compiledShadersDir() / "tint.json"));
		CHECK(ui.chain().size() == 16);
	}

	TEST_CASE("nodeRemove: the node and its params go, the host is asked to forget them; the uid is free again")
	{
		UiSession ui;
		uploadShader(ui, "brightness.frag");
		uploadLut(ui, "invert.cube");
		int rescans = ui.rescans;

		ui.edit({ { "type", "nodeRemove" }, { "uid", 0 } });
		CHECK(status(ui) == "Removed brightness");
		CHECK(ui.rescans == rescans + 1);
		CHECK(ui.chain() == std::vector<std::string>{ "1:lut:invert" });
		CHECK(ui.param("[A] brightness: Brightness").is_null());
		CHECK(ui.cleared == std::vector<clap_id>{ nodeParamId(0, 0) });

		uploadShader(ui, "tint.frag");
		CHECK(ui.chain() == std::vector<std::string>{ "1:lut:invert", "0:shader:tint" });

		ui.edit({ { "type", "nodeRemove" }, { "uid", 7 } });
		CHECK(status(ui) == "No such node");
		ui.edit({ { "type", "nodeRemove" }, { "uid", "zero" } });
		CHECK(status(ui) == "No such node");
	}

	TEST_CASE("nodeMove: the order changes, the params' ids, values and labels don't")
	{
		UiSession ui;
		uploadShader(ui, "brightness.frag");
		uploadLut(ui, "invert.cube");
		ui.receive({ { "type", "paramValue" }, { "id", nodeParamId(0, 0) }, { "value", 0.3 } });
		ui.receive({ { "type", "paramValue" }, { "id", nodeParamId(1, 0) }, { "value", 0.6 } });

		ui.edit({ { "type", "nodeMove" }, { "uid", 1 }, { "index", 0 } });
		CHECK(status(ui) == "Moved invert");
		CHECK(ui.chain() == std::vector<std::string>{ "1:lut:invert", "0:shader:brightness" });

		json params = ui.last("snapshot")["params"]; // in id order, whatever the chain's
		REQUIRE(params.size() == 3);
		CHECK(params[1]["id"] == nodeParamId(0, 0));
		CHECK(params[2]["id"] == nodeParamId(1, 0));
		CHECK(ui.value(nodeParamId(0, 0)) == 0.3);
		CHECK(ui.value(nodeParamId(1, 0)) == 0.6);
		// the labels stay (REAPER keeps an envelope's name from its creation), and the host keeps its references
		CHECK(ui.param("[B] invert: Mix")["id"] == nodeParamId(1, 0));
		CHECK(ui.param("[A] brightness: Brightness")["id"] == nodeParamId(0, 0));
		CHECK(ui.cleared.empty());

		ui.edit({ { "type", "nodeMove" }, { "uid", 1 }, { "index", 5 } }); // past the end: last
		CHECK(ui.chain() == std::vector<std::string>{ "0:shader:brightness", "1:lut:invert" });
		ui.edit({ { "type", "nodeMove" }, { "uid", 1 } });
		CHECK(status(ui) == "No index to move to");
	}

	TEST_CASE("nodeBypass: saved with the project, the params stay and the host isn't asked to rescan")
	{
		UiSession ui;
		uploadShader(ui, "brightness.frag");
		int rescans = ui.rescans;

		ui.edit({ { "type", "nodeBypass" }, { "uid", 0 }, { "bypass", true } });
		CHECK(status(ui) == "Bypassed brightness");
		CHECK(ui.rescans == rescans);
		CHECK(ui.last("snapshot")["chain"][0]["bypass"] == true);
		CHECK(ui.savedState()["chain"][0]["bypass"] == true);
		CHECK(ui.param("[A] brightness: Brightness").is_object());

		ui.edit({ { "type", "nodeBypass" }, { "uid", 0 }, { "bypass", false } });
		CHECK(status(ui) == "Enabled brightness");
		CHECK(ui.savedState()["chain"][0]["bypass"] == false);
	}

	TEST_CASE("nodeSet: another stored shader or LUT of the node's kind, with its own params")
	{
		UiSession ui;
		uploadShader(ui, "tint.frag");
		uploadShader(ui, "brightness.frag"); // uid 1
		uploadLut(ui, "invert.cube");		 // uid 2
		ui.edit({ { "type", "nodeRemove" }, { "uid", 1 } });

		ui.cleared.clear();
		ui.edit({ { "type", "nodeSet" }, { "uid", 0 }, { "name", "brightness" } });
		CHECK(status(ui) == "Loaded brightness");
		CHECK(ui.chain() == std::vector<std::string>{ "0:shader:brightness", "2:lut:invert" });
		CHECK(ui.param("[A] brightness: Brightness")["id"] == nodeParamId(0, 0));
		CHECK(ui.param("[A] tint: Amount").is_null());
		// tint's params (amount, color.x/y/z at ids 1..4) went away, or their id is brightness's now
		CHECK(ui.cleared ==
			  std::vector<clap_id>{ nodeParamId(0, 0), nodeParamId(0, 1), nodeParamId(0, 2), nodeParamId(0, 3) });

		ui.edit({ { "type", "nodeSet" }, { "uid", 2 }, { "name", "brightness" } }); // a LUT node: LUTs only
		CHECK(status(ui) == "Can't read the stored LUT brightness");
		CHECK(ui.chain() == std::vector<std::string>{ "0:shader:brightness", "2:lut:invert" });
	}

	TEST_CASE("nodeLut: a shader node's LUT, saved with the project; not for LUT nodes")
	{
		UiSession ui;
		uploadShader(ui, "lut_split.frag");
		uploadLut(ui, "invert.cube");
		uploadShader(ui, "brightness.frag");
		int rescans = ui.rescans;

		// the snapshot says which shaders sample a LUT at all
		json snapshot = ui.last("snapshot");
		CHECK(snapshot["chain"][0]["samplesLut"] == true);
		CHECK(snapshot["chain"][2]["samplesLut"] == false);
		CHECK_FALSE(snapshot["chain"][1].contains("samplesLut")); // a LUT node

		ui.edit({ { "type", "nodeLut" }, { "uid", 0 }, { "name", "invert" } });
		CHECK(status(ui) == "lut_split: LUT invert");
		CHECK(ui.rescans == rescans);
		CHECK(ui.last("snapshot")["chain"][0]["lut"] == "invert");
		json saved = ui.savedState()["chain"][0]["lut"];
		CHECK(saved["name"] == "invert");
		CHECK(gpu::lutFromJson(saved["data"]).size == 2);

		ui.edit({ { "type", "nodeLut" }, { "uid", 0 }, { "name", "" } });
		CHECK(status(ui) == "lut_split: no LUT");
		CHECK(ui.savedState()["chain"][0]["lut"]["data"].is_null());

		ui.edit({ { "type", "nodeLut" }, { "uid", 1 }, { "name", "invert" } });
		CHECK(status(ui) == "Only a shader node samples a LUT");
		ui.edit({ { "type", "nodeLut" }, { "uid", 0 }, { "name", "../../missing" } });
		CHECK(status(ui) == "Can't read the stored LUT missing");
	}

	TEST_CASE("the UI edits real values; the host gets them as 0..1 over the param's range")
	{
		UiSession ui;
		uploadShader(ui, "pixelate.frag"); // blockSize: 16, 1..128
		const clap_id blockSize = nodeParamId(0, 0);

		json param = ui.param("[A] pixelate: Block size (px)");
		CHECK(param["value"] == 16);
		CHECK(param["minValue"] == 1);
		CHECK(param["maxValue"] == 128);
		CHECK(ui.value(blockSize) == doctest::Approx(15.0 / 127));

		clap_param_info_t info{};
		REQUIRE(ui.plugin.getParamInfo(blockSize, &info));
		CHECK(info.min_value == 0);
		CHECK(info.max_value == 1);
		CHECK(info.default_value == doctest::Approx(15.0 / 127));

		ui.receive({ { "type", "paramValue" }, { "id", blockSize }, { "value", 64.5 } });
		CHECK(ui.value(blockSize) == doctest::Approx(0.5));
		clap_id id = 99;
		double value = 0;
		REQUIRE(ui.plugin.takeParamChangeForHost(id, value));
		CHECK(value == doctest::Approx(0.5));

		// the host's automation, echoed to the UI as the real value
		ui.plugin.applyHostParamValue(blockSize, 1.0);
		ui.plugin.onMainThread();
		CHECK(ui.last("paramValue")["value"] == doctest::Approx(128.0));
		CHECK(ui.savedState()["params"]["0/blockSize"] == doctest::Approx(128.0));

		// text both ways, in the real range
		char text[64];
		REQUIRE(ui.plugin.valueToText(blockSize, 0.5, text, sizeof(text)));
		CHECK(std::string(text) == "64.500");
		REQUIRE(ui.plugin.textToValue(blockSize, "1", &value));
		CHECK(value == 0.0);
	}

	TEST_CASE("a LUT node's Mix edits in the UI go to the host like any param")
	{
		UiSession ui;
		uploadLut(ui, "invert.cube");
		ui.receive({ { "type", "paramValue" }, { "id", nodeParamId(0, 0) }, { "value", 0.4 } });

		clap_id id = 99;
		double value = 0;
		REQUIRE(ui.plugin.takeParamChangeForHost(id, value));
		CHECK(id == nodeParamId(0, 0));
		CHECK(value == 0.4);
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

	TEST_CASE("the state round-trips: the chain, its values, bypass and a shader's LUT")
	{
		json saved;
		{
			UiSession ui;
			uploadShader(ui, "lut_split.frag");
			uploadLut(ui, "invert.cube");
			uploadShader(ui, "brightness.frag");
			ui.edit({ { "type", "nodeMove" }, { "uid", 2 }, { "index", 0 } });
			ui.edit({ { "type", "nodeBypass" }, { "uid", 1 }, { "bypass", true } });
			ui.edit({ { "type", "nodeLut" }, { "uid", 0 }, { "name", "invert" } });
			ui.receive({ { "type", "paramValue" }, { "id", nodeParamId(2, 0) }, { "value", 0.7 } });
			saved = ui.savedState();
		}

		UiSession ui;
		ui.loadState(saved);
		// inactive: the params are there right away (REAPER binds envelopes right after loading), none cleared
		CHECK(ui.rescans == 1);
		CHECK(ui.cleared.empty());
		CHECK(ui.chain() == std::vector<std::string>{ "2:shader:brightness", "0:shader:lut_split", "1:lut:invert" });
		CHECK(ui.value(nodeParamId(2, 0)) == 0.7);
		CHECK(ui.savedState() == saved);
	}

	TEST_CASE("a state's invalid nodes are skipped")
	{
		json lut = gpu::toJson(test::invertLut());
		UiSession ui;
		ui.loadState(
			{ { "version", 4 },
			  { "params", json::object() },
			  { "chain",
				json::array({ { { "uid", 3 }, { "kind", "lut" }, { "name", "a" }, { "data", lut } },
							  { { "uid", 3 }, { "kind", "lut" }, { "name", "same uid" }, { "data", lut } },
							  { { "uid", 16 }, { "kind", "lut" }, { "name", "uid too big" }, { "data", lut } },
							  { { "uid", 4 }, { "kind", "mesh" }, { "name", "kind" }, { "data", lut } },
							  { { "uid", 5 }, { "kind", "shader" }, { "name", "not a shader" }, { "data", lut } },
							  "not a node",
							  { { "uid", 6 }, { "kind", "lut" }, { "name", "b" }, { "data", lut } } }) } });

		CHECK(ui.chain() == std::vector<std::string>{ "3:lut:a", "6:lut:b" });
	}

	TEST_CASE("a version 3 state becomes a chain: the shader node 0, the LUT node 1, in the mode's order")
	{
		struct Case
		{
			const char* shader;
			bool lut;
			const char* mode;
			std::vector<std::string> chain;
			const char* shaderLut; // node 0's LUT
		};
		const Case cases[] = {
			{ "brightness.frag", true, "before", { "1:lut:invert", "0:shader:brightness" }, "" },
			{ "brightness.frag", true, "after", { "0:shader:brightness", "1:lut:invert" }, "" },
			{ "brightness.frag", true, "shader", { "0:shader:brightness" }, "invert" },
			{ "brightness.frag", false, "after", { "0:shader:brightness" }, "" },
			{ nullptr, true, "shader", { "1:lut:invert" }, nullptr },
			{ nullptr, false, "after", {}, nullptr },
		};

		for (const Case& c : cases)
		{
			INFO("shader ", c.shader ? c.shader : "none", ", LUT ", c.lut, ", mode ", c.mode);
			UiSession ui;
			ui.loadState(
				stateV3(c.shader, c.lut, c.mode, { { "Audio Gain", 0.5 }, { "brightness", 0.3 }, { "LUT Mix", 0.6 } }));

			CHECK(ui.chain() == c.chain);
			json snapshot = ui.last("snapshot");
			for (const json& node : snapshot["chain"])
				if (c.shaderLut && node["uid"] == 0)
					CHECK(node["lut"] == c.shaderLut);
			CHECK(ui.value(Parameters::AudioGain) == 0.5);
			// the params keep their version 3 ids
			if (c.shader)
				CHECK(ui.value(nodeParamId(0, 0)) == 0.3);
			if (c.lut && std::string(c.mode) != "shader")
				CHECK(ui.value(nodeParamId(1, 0)) == 0.6);
			CHECK(ui.savedState()["version"] == 4);
		}
	}

	TEST_CASE("an unrecognized state keeps the current chain")
	{
		UiSession ui;
		uploadLut(ui, "invert.cube");
		ui.loadState({ { "version", 2 } });
		ui.loadState("not a state");
		CHECK(ui.chain() == std::vector<std::string>{ "0:lut:invert" });
	}

	TEST_CASE("unknown, old and malformed messages are ignored")
	{
		UiSession ui;
		ui.plugin.handleWebUIMessage("not json");
		ui.plugin.handleWebUIMessage("[1, 2]");
		ui.receive({ { "type", "noSuchMessage" } });
		ui.receive({ { "type", "shaderSelect" }, { "name", "" } });
		ui.receive({ { "type", "lutMode" }, { "mode", "before" } });
		ui.receive({ { "value", 1 } });

		CHECK(ui.sent.empty());
		CHECK(ui.callbacks == 0);
	}
}
