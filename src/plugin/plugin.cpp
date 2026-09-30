/**
 * @file
 * @brief ReaShaderPlugin: params, state, the chain, the web UI protocol, and the REAPER video tap.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "plugin/plugin.h"

#include "render/lut_file.h"
#include "render/renderer.h"
#include "render/shader_compiler.h"
#include "util/logging.h"
#include "util/paths.h"
#include "util/shell.h"

#include "reaper_plugin.h"
#include "wdltypes.h" // video_frame.h needs WDL_FIXALIGN/INT_PTR but doesn't include this itself
#include "video_frame.h"
#include "video_processor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>

namespace ReaShader
{
	using Parameters::json;

	namespace
	{
		// uploads are stored as <dir>/<name>.json: compiled shaders, parsed LUTs
		std::filesystem::path storedPath(const std::filesystem::path& dir, const std::string& name)
		{
			return dir / (name + ".json");
		}

		std::vector<std::string> storedNames(const std::filesystem::path& dir)
		{
			std::vector<std::string> names;
			std::error_code error;
			for (const auto& entry : std::filesystem::directory_iterator(dir, error))
			{
				if (entry.path().extension() == ".json")
					names.push_back(entry.path().stem().string());
			}
			std::sort(names.begin(), names.end());
			return names;
		}

		std::string readFile(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			std::stringstream content;
			content << file.rdbuf();
			return content.str();
		}

		void writeFile(const std::filesystem::path& path, const std::string& content)
		{
			std::filesystem::create_directories(path.parent_path());
			std::ofstream file(path, std::ios::binary);
			file << content;
			if (!file)
				throw std::runtime_error("Can't write " + path.string());
		}

		using Node = ReaShaderPlugin::Node;

		// node kinds, as named in the protocol and the state
		const char* kindName(Node::Kind kind)
		{
			return kind == Node::Kind::Shader ? "shader" : "lut";
		}

		std::optional<Node::Kind> kindFromName(const std::string& name)
		{
			if (name == "shader")
				return Node::Kind::Shader;
			if (name == "lut")
				return Node::Kind::Lut;
			return std::nullopt;
		}

		// where uploads of a kind are stored
		std::filesystem::path storedDir(Node::Kind kind)
		{
			return kind == Node::Kind::Shader ? util::paths::compiledShadersDir() : util::paths::lutsDir();
		}

		// a name from the UI: only the file name counts, never a path ("../../x" is "x")
		std::string fileNameOnly(const std::string& name)
		{
			return std::filesystem::path(name).filename().string();
		}

		// a stored shader or LUT's JSON, by name; throws when it can't be read
		std::string readStored(Node::Kind kind, const std::string& name)
		{
			std::string data = name.empty() ? "" : readFile(storedPath(storedDir(kind), name));
			if (data.empty())
				throw std::runtime_error(std::string("Can't read the stored ") +
										 (kind == Node::Kind::Shader ? "shader " : "LUT ") + name);
			return data;
		}

		// a shader with more sliders than a node's slots is rejected, never loaded in part
		void checkSliders(const gpu::CompiledShader& shader, const std::string& name)
		{
			if (shader.params.size() > Parameters::kNodeSlots)
				throw std::runtime_error(std::format("{} has {} sliders, and a shader can have at most {}", name,
													 shader.params.size(), Parameters::kNodeSlots));
		}

		// a node's content from a stored JSON: its name, data and parsed form; throws on invalid data, or a
		// shader with too many sliders
		void setContent(Node& node, const std::string& name, const std::string& data)
		{
			try
			{
				if (node.kind == Node::Kind::Shader)
					node.shader = std::make_shared<const gpu::CompiledShader>(gpu::fromJson(json::parse(data)));
				else
					node.lut = std::make_shared<const gpu::LutData>(gpu::lutFromJson(json::parse(data)));
			}
			catch (const std::exception& e)
			{
				throw std::runtime_error(std::string("Invalid stored ") +
										 (node.kind == Node::Kind::Shader ? "shader " : "LUT ") + name + ": " +
										 e.what());
			}
			if (node.shader)
				checkSliders(*node.shader, name);
			node.name = name;
			node.data = data;
		}

		// a shader node's iChannel1 from a stored LUT's JSON; no name: none (an identity)
		void setShaderLut(Node& node, const std::string& name, const std::string& data)
		{
			if (name.empty())
			{
				node.shaderLut.reset();
				node.lutName.clear();
				node.lutData.clear();
				return;
			}
			Node table;
			table.kind = Node::Kind::Lut;
			setContent(table, name, data);
			node.shaderLut = table.lut;
			node.lutName = name;
			node.lutData = data;
		}

		Node* findNode(std::vector<Node>& chain, uint32_t uid)
		{
			for (Node& node : chain)
				if (node.uid == uid)
					return &node;
			return nullptr;
		}

		// a whole number >= 0 in `msg`; none for a missing or invalid one
		std::optional<size_t> countOf(const json& msg, const char* key)
		{
			const json value = msg.value(key, json());
			if (!value.is_number_integer() || value.get<int64_t>() < 0)
				return std::nullopt;
			return value.get<size_t>();
		}

		// the uid in a message or a saved node; none for a missing or invalid one
		std::optional<uint32_t> uidOf(const json& msg)
		{
			std::optional<size_t> uid = countOf(msg, "uid");
			if (!uid || *uid >= Parameters::kMaxNodes)
				return std::nullopt;
			return (uint32_t)*uid;
		}

		// A node's tag in labels and the UI, "A".."P": its uid as a letter, so it isn't taken for a position
		std::string tagOf(uint32_t uid)
		{
			return std::string(1, (char)('A' + uid));
		}

		// A new node with a stored shader or LUT, at `index` (past the end: last); returns the error, if any.
		// Its uid is the smallest free one. A removed node's uid comes back at once, on purpose: REAPER keeps
		// a removed node's envelopes and modulation on its param ids, and they'd drive the next node there
		// sooner or later anyway, so it's better seen right away.
		std::string insertNode(std::vector<Node>& chain, Node::Kind kind, const std::string& name,
							   const std::string& data, size_t index)
		{
			uint32_t uid = 0;
			while (uid < Parameters::kMaxNodes && findNode(chain, uid))
				uid++;
			if (uid == Parameters::kMaxNodes)
				return std::format("The chain is full ({} nodes)", Parameters::kMaxNodes);

			Node node;
			node.uid = uid;
			node.kind = kind;
			setContent(node, name, data);
			chain.insert(chain.begin() + (std::ptrdiff_t)(index < chain.size() ? index : chain.size()),
						 std::move(node));
			return {};
		}

		// Each node's params, in chain order: a shader's sliders, a LUT's Mix.
		// Names (state keys) are "<uid>/<member>", labels "[<tag>] <node name>: <label>": both stay the same for
		// the node's life, because REAPER keeps an envelope's name from when it was created, and the tag tells
		// the same shader twice apart.
		std::vector<Parameters::NodeParams> paramsOfNodes(const std::vector<Node>& chain)
		{
			std::vector<Parameters::NodeParams> result;
			for (const Node& node : chain)
			{
				Parameters::NodeParams nodeParams{ node.uid, {} };
				std::string prefix = std::to_string(node.uid) + "/";
				std::string labelPrefix = std::format("[{}] {}: ", tagOf(node.uid), node.name);
				if (node.shader)
				{
					for (const gpu::ShaderParamField& field : node.shader->params)
					{
						Parameters::Param param;
						param.name = prefix + field.name;
						param.label = labelPrefix + field.label;
						param.defaultValue = field.defaultValue;
						param.minValue = field.minValue;
						param.maxValue = field.maxValue;
						nodeParams.params.push_back(std::move(param));
					}
				}
				else
				{
					Parameters::Param mix; // 0 = the frame as is, 1 = fully through the LUT
					mix.name = prefix + "mix";
					mix.label = labelPrefix + "Mix";
					mix.units = "%";
					mix.defaultValue = 1.0;
					nodeParams.params.push_back(std::move(mix));
				}
				result.push_back(std::move(nodeParams));
			}
			return result;
		}

		// The renderer's chain, each node with its params' place in the param list: its uid's slots, at most
		// kNodeSlots (as ParamList lays out paramsOfNodes())
		std::vector<ReaShaderRenderer::ChainNode> rendererChain(const std::vector<Node>& chain)
		{
			std::vector<ReaShaderRenderer::ChainNode> result;
			for (const Node& node : chain)
			{
				size_t count = node.shader ? node.shader->params.size() : 1;
				if (count > Parameters::kNodeSlots)
					count = Parameters::kNodeSlots;
				result.push_back({ .uid = node.uid,
								   .shader = node.shader,
								   .lut = node.lut,
								   .shaderLut = node.shaderLut,
								   .bypass = node.bypass,
								   .firstParam = Parameters::nodeParamId(node.uid, 0),
								   .paramCount = count });
			}
			return result;
		}

		// A version 3 state (one shader, one LUT and a LUT mode) as version 4: the shader is node 0 and the
		// LUT node 1 (their params keep their ids), in the mode's order; mode "shader" attaches the LUT to the
		// shader node. Param names get their node's prefix, and "LUT Mix" becomes the LUT node's Mix.
		json migrateV3(const json& v3)
		{
			const json shader = v3.value("shader", json::object());
			const json lut = v3.value("lut", json::object());
			const json compiled = shader.value("compiled", json());
			const json table = lut.value("data", json());
			std::string mode = lut.value("mode", "after");
			bool lutInShader = compiled.is_object() && table.is_object() && mode == "shader";

			json shaderNode = { { "uid", 0 },
								{ "kind", "shader" },
								{ "name", shader.value("name", "") },
								{ "bypass", false },
								{ "data", compiled },
								{ "lut",
								  { { "name", lutInShader ? lut.value("name", "") : "" },
									{ "data", lutInShader ? table : json() } } } };
			json lutNode = { { "uid", 1 },
							 { "kind", "lut" },
							 { "name", lut.value("name", "") },
							 { "bypass", false },
							 { "data", table } };
			json chain = json::array();
			if (table.is_object() && !lutInShader && mode == "before")
				chain.push_back(lutNode);
			if (compiled.is_object())
				chain.push_back(shaderNode);
			if (table.is_object() && !lutInShader && mode != "before")
				chain.push_back(lutNode);

			json params = json::object();
			const json oldParams = v3.value("params", json::object());
			for (const auto& [name, value] : oldParams.items())
			{
				if (name == "Audio Gain")
					params[name] = value;
				else if (name == "LUT Mix")
					params["1/mix"] = value;
				else
					params["0/" + name] = value;
			}

			return { { "version", 4 },
					 { "params", params },
					 { "device", v3.value("device", 0) },
					 { "logo", v3.value("logo", false) },
					 { "chain", chain } };
		}
	} // namespace

	ReaShaderPlugin::ReaShaderPlugin() = default;
	ReaShaderPlugin::~ReaShaderPlugin() = default;

	void ReaShaderPlugin::initialize(const clap_host_t* clapHost)
	{
		host = clapHost;

		// no shader until the user picks one: video passes through
		// Vulkan isn't touched until activate()
		reaShaderRenderer = std::make_unique<ReaShaderRenderer>(this);
	}

	void ReaShaderPlugin::activate()
	{
		auto* reaperInfo =
			static_cast<reaper_plugin_info_t*>(const_cast<void*>(host->get_extension(host, "cockos.reaper_extension")));

		if (!reaperInfo || !reaperInfo->GetFunc)
			return; // not hosted by REAPER

		using ClapGetReaperContext = void* (*)(const clap_host_t*, int);
		using VideoCreateVideoProcessor = IREAPERVideoProcessor* (*)(void*, int);
		using GetSetMediaTrackInfo = void* (*)(void* track, const char* parmname, void* setNewValue);
		using GetMediaTrackInfo_Value = double (*)(void* track, const char* parmname);

		auto clapGetReaperContext = (ClapGetReaperContext)reaperInfo->GetFunc("clap_get_reaper_context");
		auto videoCreateVideoProcessor = (VideoCreateVideoProcessor)reaperInfo->GetFunc("video_CreateVideoProcessor");
		if (!clapGetReaperContext)
			return;

		// video tap: context 4 = FxDsp
		void* fxDsp = clapGetReaperContext(host, 4);
		if (fxDsp && videoCreateVideoProcessor)
		{
			videoProcessor = videoCreateVideoProcessor(fxDsp, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION);
			if (videoProcessor)
			{
				videoProcessor->userdata = this;
				videoProcessor->process_frame = _processVideoFrame;
				videoProcessor->get_parameter_value = _getVideoParam;
			}
		}

		// track info: context 1 = parent track
		if (void* track = clapGetReaperContext(host, 1))
		{
			auto getTrackInfo = (GetSetMediaTrackInfo)reaperInfo->GetFunc("GetSetMediaTrackInfo");
			auto getTrackValue = (GetMediaTrackInfo_Value)reaperInfo->GetFunc("GetMediaTrackInfo_Value");

			std::lock_guard lock(stateMutex);
			if (getTrackInfo)
			{
				auto* name = (const char*)getTrackInfo(track, "P_NAME", nullptr); // null on master
				trackName = name ? name : "";
			}
			if (getTrackValue)
				trackNumber = (int)getTrackValue(track, "IP_TRACKNUMBER");
		}

		reaShaderRenderer->init(); // no-op after the first time
		active = true;
		_webuiSendSnapshot();
	}

	void ReaShaderPlugin::deactivate()
	{
		active = false;

		// stop REAPER's video callbacks (the renderer lives on until the plugin is destroyed)
		delete videoProcessor;
		videoProcessor = nullptr;
	}

	void ReaShaderPlugin::onMainThread()
	{
		if (hostParamsChanged.exchange(false))
			_notifyHostParams();

		if (hostChangedParams.exchange(false))
		{
			for (const Parameters::Param& p : params.list())
				_webuiSend({ { "type", "paramValue" }, { "id", p.id }, { "value", p.toReal(params.value(p.id)) } });
		}
	}

	// -------- clap.params --------

	uint32_t ReaShaderPlugin::paramCount() const
	{
		return Parameters::kParamCount;
	}

	// Every param, index = id. An unused node slot is hidden with an empty name: REAPER then leaves it out of
	// its menus entirely (a hidden param with a name is listed, greyed).
	bool ReaShaderPlugin::getParamInfo(uint32_t index, clap_param_info_t* info) const
	{
		if (index >= Parameters::kParamCount)
			return false;
		Parameters::Param param = params.at(index);

		// 0..1 over the param's own range (see Param::toHost); value_to_text shows the real value
		*info = {};
		info->id = param.id;
		info->flags = CLAP_PARAM_IS_AUTOMATABLE | (param.used() ? 0 : CLAP_PARAM_IS_HIDDEN);
		std::snprintf(info->name, sizeof(info->name), "%s", param.label.c_str());
		info->min_value = 0.0;
		info->max_value = 1.0;
		info->default_value = param.toHost(param.defaultValue);
		return true;
	}

	bool ReaShaderPlugin::getParamValue(clap_id id, double* value) const
	{
		if (id >= Parameters::kParamCount)
			return false;
		*value = params.value(id);
		return true;
	}

	// a host value (0..1) as the param's real value, like the web UI shows it: "64.500", or "50.0 %"
	bool ReaShaderPlugin::valueToText(clap_id id, double value, char* buffer, uint32_t size) const
	{
		if (id >= Parameters::kParamCount)
			return false;
		Parameters::Param param = params.at(id);
		double real = param.toReal(value);
		if (param.units == "%")
			std::snprintf(buffer, size, "%.1f %%", real * 100.0);
		else
			std::snprintf(buffer, size, "%.3f", real);
		return true;
	}

	// a real value typed in the host (a percentage for "%" params) as a host value (0..1)
	bool ReaShaderPlugin::textToValue(clap_id id, const char* text, double* value) const
	{
		if (id >= Parameters::kParamCount)
			return false;
		char* end = nullptr;
		double parsed = std::strtod(text, &end);
		if (end == text)
			return false;
		Parameters::Param param = params.at(id);
		*value = param.toHost(param.units == "%" ? parsed / 100.0 : parsed);
		return true;
	}

	void ReaShaderPlugin::applyHostParamValue(clap_id id, double value)
	{
		if (id >= Parameters::kParamCount)
			return;
		params.setValue(id, value);
		hostChangedParams = true;
		host->request_callback(host);
	}

	bool ReaShaderPlugin::takeParamChangeForHost(clap_id& id, double& value)
	{
		return params.takeFlaggedForHost(id, value);
	}

	double ReaShaderPlugin::getAudioGain() const
	{
		return params.value(Parameters::AudioGain);
	}

	// -------- clap.state --------
	//
	// One JSON document:
	// { "version": 4, "params": { "<name>": value }, "device": n, "logo": false,
	//   "chain": [ { "uid": 0, "kind": "shader", "name": "", "bypass": false, "data": {...},
	//                "lut": { "name": "", "data": null } },          <- shader nodes only: their iChannel1
	//              { "uid": 1, "kind": "lut", "name": "", "bypass": false, "data": {...} } ] }
	// The stored shaders and LUTs are embedded, so a project doesn't depend on the plugin's folders.
	// Version 3 (one shader, one LUT, a LUT mode) is migrated (migrateV3); other versions change nothing.

	constexpr int kStateVersion = 4;

	bool ReaShaderPlugin::saveState(const clap_ostream_t* stream)
	{
		json state;
		{
			std::lock_guard lock(stateMutex);
			json nodes = json::array();
			for (const Node& node : chain)
			{
				json saved = { { "uid", node.uid },
							   { "kind", kindName(node.kind) },
							   { "name", node.name },
							   { "bypass", node.bypass },
							   { "data", json::parse(node.data, nullptr, false) } };
				if (node.kind == Node::Kind::Shader)
					saved["lut"] = { { "name", node.lutName },
									 { "data",
									   node.lutName.empty() ? json() : json::parse(node.lutData, nullptr, false) } };
				nodes.push_back(std::move(saved));
			}
			state = { { "version", kStateVersion },
					  { "params", params.valuesToJson() },
					  { "device", renderingDevice },
					  { "logo", showLogo },
					  { "chain", nodes } };
		}

		std::string data = state.dump();
		size_t written = 0;
		while (written < data.size())
		{
			int64_t n = stream->write(stream, data.data() + written, data.size() - written);
			if (n <= 0)
			{
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderPlugin", "State save error", "Stream write failed");
				return false;
			}
			written += (size_t)n;
		}
		return true;
	}

	bool ReaShaderPlugin::loadState(const clap_istream_t* stream)
	{
		std::string data;
		char buffer[4096];
		int64_t n;
		while ((n = stream->read(stream, buffer, sizeof(buffer))) > 0)
			data.append(buffer, (size_t)n);

		json state = json::parse(data, nullptr, /* allow_exceptions */ false);
		int version = state.is_object() ? state.value("version", 0) : 0;
		if (version == 3)
			state = migrateV3(state);
		else if (version != kStateVersion)
		{
			// unrecognized format or version: keep the defaults
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "State load", "Unrecognized state, using defaults");
			return true;
		}

		const json savedParams = state.value("params", json::object());
		params.valuesFromJson(savedParams);

		int device = state.value("device", 0);
		bool logo = state.value("logo", false);
		bool deviceChanged;
		{
			std::lock_guard lock(stateMutex);
			deviceChanged = device != renderingDevice;
			renderingDevice = device;
			showLogo = logo;
		}
		Parameters::ValueMap savedValues;
		for (const auto& [name, value] : savedParams.items())
		{
			if (value.is_number())
				savedValues[name] = value.get<double>();
		}

		if (deviceChanged)
			reaShaderRenderer->changeRenderingDevice(device); // no-op when not active
		reaShaderRenderer->setLogoEnabled(logo);

		_loadChain(state.value("chain", json::array()), savedValues);

		// the host learns the new names and values now: REAPER binds the project's envelopes right after this
		hostParamsChanged = false;
		_notifyHostParams();

		_webuiSendSnapshot();
		return true;
	}

	void ReaShaderPlugin::_loadChain(const json& chainState, const Parameters::ValueMap& savedValues)
	{
		std::vector<std::string> skipped;
		std::string error = _editChain(
			[&](std::vector<Node>& nodes) -> std::string {
				nodes.clear();
				if (!chainState.is_array())
					return {};
				for (const json& saved : chainState)
				{
					try
					{
						std::optional<Node::Kind> kind =
							saved.is_object() ? kindFromName(saved.value("kind", "")) : std::nullopt;
						std::optional<uint32_t> uid = saved.is_object() ? uidOf(saved) : std::nullopt;
						if (!kind || !uid || findNode(nodes, *uid))
							throw std::runtime_error("not a node: " + saved.dump().substr(0, 80));

						Node node;
						node.uid = *uid;
						node.kind = *kind;
						node.bypass = saved.value("bypass", false);
						setContent(node, saved.value("name", ""), saved.value("data", json()).dump());
						const json lut = saved.value("lut", json::object());
						const json table = lut.is_object() ? lut.value("data", json()) : json();
						if (node.kind == Node::Kind::Shader && table.is_object())
							setShaderLut(node, lut.value("name", ""), table.dump());
						nodes.push_back(std::move(node));
					}
					catch (const std::exception& e)
					{
						LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "State load: node skipped", e.what());
						skipped.push_back(e.what());
					}
				}
				return {};
			},
			ParamsChange::Load, savedValues);

		if (!error.empty())
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "State load: chain failed", error);
			_webuiSendChainStatus(error, "error");
		}
		else if (!skipped.empty())
		{
			std::string status = "Left out of the project's chain:";
			for (const std::string& reason : skipped)
				status += "\n- " + reason;
			_webuiSendChainStatus(status, "error");
		}
	}

	// -------- REAPER video tap --------

	// Renders one frame. The input frame from renderInputVideoFrame() is immutable: it must be
	// returned as-is or Release()d. The result goes into a separate frame from newVideoFrame().
	IVideoFrame* ReaShaderPlugin::_processVideoFrame(IREAPERVideoProcessor* videoProcessor, const double* parmlist,
													 int nparms, double projectTime, double frameRate, int)
	{
		auto* plugin = static_cast<ReaShaderPlugin*>(videoProcessor->userdata);

		IVideoFrame* input = videoProcessor->renderInputVideoFrame(0, 'RGBA');
		if (!input)
			return nullptr;

		int w = input->get_w();
		int h = input->get_h();

		IVideoFrame* output = videoProcessor->newVideoFrame(w, h, 'RGBA');
		if (!output)
			return input;

		// param values at video time, by id (= index): parmlist[0] is wet/dry, then param i is at parmlist[i + 1]
		// (params REAPER doesn't pass fall back to the current value)
		double paramValues[Parameters::kParamCount];
		const size_t paramCount = Parameters::kParamCount;
		for (size_t i = 0; i < paramCount; i++)
			paramValues[i] = (int)i + 1 < nparms ? parmlist[i + 1] : plugin->params.value((Parameters::Id)i);

		FrameView inputFrame{ w, h, input->get_rowspan(), reinterpret_cast<uint8_t*>(input->get_bits()) };
		FrameView outputFrame{ w, h, output->get_rowspan(), reinterpret_cast<uint8_t*>(output->get_bits()) };
		ReaShaderRenderer::FrameInputs inputs{ projectTime, frameRate, paramValues, paramCount };

		// false: inactive, busy, failed, or no pass and no logo -> pass the input through
		if (!plugin->reaShaderRenderer->renderFrame(inputFrame, outputFrame, inputs))
		{
			output->Release();
			return input;
		}

		input->Release();
		return output;
	}

	bool ReaShaderPlugin::_getVideoParam(IREAPERVideoProcessor* videoProcessor, int idx, double* valueOut)
	{
		auto* plugin = static_cast<ReaShaderPlugin*>(videoProcessor->userdata);
		if (idx < 0 || (size_t)idx >= Parameters::kParamCount)
			return false;
		*valueOut = plugin->params.value((Parameters::Id)idx);
		return true;
	}

	// -------- renderer support --------

	int ReaShaderPlugin::getRenderingDeviceIndex() const
	{
		std::lock_guard lock(stateMutex);
		return renderingDevice;
	}

	void ReaShaderPlugin::setRenderingDeviceIndex(int index)
	{
		std::lock_guard lock(stateMutex);
		renderingDevice = index;
	}

	void ReaShaderPlugin::setRenderingDevicesList(const std::vector<std::string>& deviceNames)
	{
		std::lock_guard lock(stateMutex);
		renderingDeviceNames = deviceNames;
	}

	void ReaShaderPlugin::_notifyHostParams()
	{
		std::vector<Parameters::Id> clears;
		{
			std::lock_guard lock(stateMutex);
			clears.swap(paramsToClear);
		}

		auto* hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
		if (!hostParams)
			return;
		// params that went away, or whose id now holds another param: the host's automation and modulation of
		// them would otherwise drive whatever takes the id next
		for (Parameters::Id id : clears)
			hostParams->clear(host, id,
							  CLAP_PARAM_CLEAR_ALL | CLAP_PARAM_CLEAR_AUTOMATIONS | CLAP_PARAM_CLEAR_MODULATIONS);
		hostParams->rescan(host, CLAP_PARAM_RESCAN_INFO | CLAP_PARAM_RESCAN_VALUES);
	}

	// -------- the chain --------
	//
	// An ordered list of nodes, each a stored shader or LUT. Shaders are compiled and LUTs parsed once, when
	// uploaded, and stored in their folder as <name>.json; nodes load the stored form, and the chain is also
	// embedded in the project state. Every change goes through _editChain.

	std::string ReaShaderPlugin::_editChain(const std::function<std::string(std::vector<Node>&)>& edit,
											ParamsChange paramsChange, const Parameters::ValueMap& savedValues)
	{
		std::lock_guard chainLock(chainMutex);
		std::vector<Node> edited;
		{
			std::lock_guard lock(stateMutex);
			edited = chain;
		}

		std::string error;
		try
		{
			error = edit(edited);
		}
		catch (const std::exception& e)
		{
			error = e.what();
		}
		if (error.empty())
			error = reaShaderRenderer->setChain(rendererChain(edited));
		if (!error.empty())
			return error;

		if (paramsChange != ParamsChange::None)
		{
			std::vector<Parameters::Id> gone = params.replaceNodeParams(paramsOfNodes(edited), savedValues);
			std::lock_guard lock(stateMutex);
			if (paramsChange == ParamsChange::Edit)
				paramsToClear.insert(paramsToClear.end(), gone.begin(), gone.end());
		}
		{
			std::lock_guard lock(stateMutex);
			chain = std::move(edited);
		}
		if (paramsChange != ParamsChange::None)
		{
			hostParamsChanged = true;
			host->request_callback(host); // -> onMainThread() -> _notifyHostParams()
		}
		return {};
	}

	void ReaShaderPlugin::_upload(Node::Kind kind, const std::string& fileName, const std::string& source)
	{
		std::string name = std::filesystem::path(fileName).stem().string();
		if (name.empty())
			name = kindName(kind);

		std::string data;
		try
		{
			if (kind == Node::Kind::Shader)
			{
				gpu::CompiledShader shader = gpu::compileShader(source, fileName);
				checkSliders(shader, name); // not stored: it could never be added
				data = gpu::toJson(shader).dump();
			}
			else
				data = gpu::toJson(gpu::parseCube(source, fileName)).dump();
			writeFile(storedPath(storedDir(kind), name), data);
		}
		catch (const std::exception& e)
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Upload failed", e.what());
			_webuiSendChainStatus(e.what(), "error");
			return;
		}

		std::string error =
			_editChain([&](std::vector<Node>& nodes) { return insertNode(nodes, kind, name, data, nodes.size()); },
					   ParamsChange::Edit);
		_chainEdited(error, "Added " + name);
	}

	void ReaShaderPlugin::_chainEdited(const std::string& error, const std::string& done)
	{
		if (!error.empty())
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Chain change failed", error);
			_webuiSendChainStatus(error, "error");
		}
		else
			_webuiSendChainStatus(done, "ok");
		_webuiSendSnapshot(); // also after an error: an upload's file may be new in the lists
	}

	// -------- web UI --------
	//
	// Messages to the UI:
	// - snapshot    { version, track, params, devices, logo, chain, shaders, luts }: everything, the UI rebuilds
	//                itself from it; chain = [{ uid, tag, kind, name, bypass, lut, samplesLut }] (lut, samplesLut:
	//                shader nodes only: its LUT's name, and whether the shader samples a LUT at all)
	// - paramValue  { id, value }: a host automation change
	// - chainStatus { status, state }: state is "busy", "ok" or "error"
	//
	// Messages from the UI (names are stored files' names, never paths):
	// - ready       {}: the page loaded, send a snapshot
	// - paramValue  { id, value }
	// - renderingDevice { index }
	// - logo        { enabled }: the spinning logo over the video (shown with the about box)
	// - openUrl     { url }: opens an https:// link in the system browser
	// - shaderUpload{ name, source }: GLSL to compile and store, then append as a node
	// - lutUpload   { name, source }: a .cube file's text to parse and store, then append as a node
	// - nodeAdd     { kind, name, index }: a stored shader or LUT ("shader" / "lut") as a new node at index
	//                (none, or past the end: last)
	// - nodeRemove  { uid }
	// - nodeMove    { uid, index }
	// - nodeBypass  { uid, bypass }
	// - nodeSet     { uid, name }: another stored shader or LUT of the node's kind
	// - nodeLut     { uid, name }: a shader node's iChannel1, a stored LUT, "" = none

	void ReaShaderPlugin::setWebUISender(WebUISender sender)
	{
		std::lock_guard lock(webUISenderMutex);
		webUISender = std::move(sender);
	}

	void ReaShaderPlugin::clearWebUISender()
	{
		std::lock_guard lock(webUISenderMutex);
		webUISender = nullptr;
	}

	void ReaShaderPlugin::_webuiSend(const json& msg)
	{
		// called under the lock, so clearWebUISender() waits for an in-flight send to finish
		std::lock_guard lock(webUISenderMutex);
		if (webUISender)
			webUISender(msg.dump());
	}

	void ReaShaderPlugin::_webuiSendSnapshot()
	{
		json msg;
		{
			std::lock_guard lock(stateMutex);

			std::string track = trackNumber == -1 ? "MASTER"
								: trackNumber == 0 ? "Track Not Found"
												   : trackName;

			json nodes = json::array();
			for (const Node& node : chain)
			{
				json shown = { { "uid", node.uid },
							   { "tag", tagOf(node.uid) },
							   { "kind", kindName(node.kind) },
							   { "name", node.name },
							   { "bypass", node.bypass } };
				if (node.kind == Node::Kind::Shader)
				{
					shown["lut"] = node.lutName;
					shown["samplesLut"] = node.shader->samplesLut;
				}
				nodes.push_back(std::move(shown));
			}

			msg = { { "type", "snapshot" },
					{ "version", REASHADER_VERSION },
					{ "track", { { "number", trackNumber }, { "name", track } } },
					{ "devices", { { "names", renderingDeviceNames }, { "selected", renderingDevice } } },
					{ "logo", showLogo },
					{ "chain", nodes } };
		}
		msg["params"] = params.toJson();
		msg["shaders"] = storedNames(util::paths::compiledShadersDir());
		msg["luts"] = storedNames(util::paths::lutsDir());

		_webuiSend(msg);
	}

	void ReaShaderPlugin::_webuiSendChainStatus(const std::string& status, const char* state)
	{
		_webuiSend({ { "type", "chainStatus" }, { "status", status }, { "state", state } });
	}

	void ReaShaderPlugin::handleWebUIMessage(const std::string& text)
	{
		json msg = json::parse(text, nullptr, /* allow_exceptions */ false);
		const std::string type = msg.is_object() ? msg.value("type", "") : "";

		if (type == "ready")
		{
			_webuiSendSnapshot();
		}
		else if (type == "paramValue")
		{
			auto param = params.find(msg.value("id", Parameters::Id(-1)));
			if (!param)
				return;

			params.setRealValue(param->id, msg.value("value", param->defaultValue)); // the UI's are real values
			params.flagForHost(param->id);
			auto* hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
			if (hostParams)
				hostParams->request_flush(host);
		}
		else if (type == "renderingDevice")
		{
			int index = msg.value("index", 0);
			setRenderingDeviceIndex(index);
			reaShaderRenderer->changeRenderingDevice(index);
			_webuiSendSnapshot();
		}
		else if (type == "logo")
		{
			bool enabled = msg.value("enabled", false);
			{
				std::lock_guard lock(stateMutex);
				showLogo = enabled;
			}
			reaShaderRenderer->setLogoEnabled(enabled);
		}
		else if (type == "openUrl")
		{
			util::shell::openUrl(msg.value("url", ""));
		}
		else if (type == "shaderUpload" || type == "lutUpload")
		{
			_upload(type == "shaderUpload" ? Node::Kind::Shader : Node::Kind::Lut, msg.value("name", ""),
					msg.value("source", ""));
		}
		else if (type == "nodeAdd")
		{
			std::optional<Node::Kind> kind = kindFromName(msg.value("kind", ""));
			std::string name = fileNameOnly(msg.value("name", ""));
			size_t at = countOf(msg, "index").value_or(Parameters::kMaxNodes);
			std::string error = _editChain(
				[&](std::vector<Node>& nodes) -> std::string {
					if (!kind)
						return "Unknown node kind";
					return insertNode(nodes, *kind, name, readStored(*kind, name), at);
				},
				ParamsChange::Edit);
			_chainEdited(error, "Added " + name);
		}
		else if (type == "nodeRemove" || type == "nodeMove" || type == "nodeBypass" || type == "nodeSet" ||
				 type == "nodeLut")
		{
			std::optional<uint32_t> uid = uidOf(msg);
			std::string done;
			// bypass and a shader's LUT leave the params as they are
			ParamsChange paramsChange =
				type == "nodeBypass" || type == "nodeLut" ? ParamsChange::None : ParamsChange::Edit;
			std::string error = _editChain(
				[&](std::vector<Node>& nodes) -> std::string {
					Node* node = uid ? findNode(nodes, *uid) : nullptr;
					if (!node)
						return "No such node";
					done = node->name;

					if (type == "nodeRemove")
					{
						nodes.erase(nodes.begin() + (node - nodes.data()));
						done = "Removed " + done;
					}
					else if (type == "nodeMove")
					{
						std::optional<size_t> index = countOf(msg, "index");
						if (!index)
							return "No index to move to";
						Node moved = std::move(*node);
						nodes.erase(nodes.begin() + (node - nodes.data()));
						size_t at = *index;
						nodes.insert(nodes.begin() + (std::ptrdiff_t)(at < nodes.size() ? at : nodes.size()),
									 std::move(moved));
						done = "Moved " + done;
					}
					else if (type == "nodeBypass")
					{
						node->bypass = msg.value("bypass", false);
						done = (node->bypass ? "Bypassed " : "Enabled ") + done;
					}
					else if (type == "nodeSet")
					{
						std::string name = fileNameOnly(msg.value("name", ""));
						setContent(*node, name, readStored(node->kind, name));
						done = "Loaded " + name;
					}
					else // nodeLut
					{
						if (node->kind != Node::Kind::Shader)
							return "Only a shader node samples a LUT";
						std::string name = fileNameOnly(msg.value("name", ""));
						setShaderLut(*node, name, name.empty() ? "" : readStored(Node::Kind::Lut, name));
						done = name.empty() ? done + ": no LUT" : done + ": LUT " + name;
					}
					return {};
				},
				paramsChange);
			_chainEdited(error, done);
		}
		else
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Unexpected message from web UI", text);
		}
	}
} // namespace ReaShader
