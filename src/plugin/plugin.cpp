/**
 * @file
 * @brief ReaShaderPlugin: params, state, the web UI protocol, shaders, and the REAPER video tap.
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

		// the LUT modes, as named in the protocol and the state
		bool isLutMode(const std::string& mode)
		{
			return mode == "before" || mode == "after" || mode == "shader";
		}

		// a shader's sliders, in the order of its Params fields
		std::vector<Parameters::Param> paramsOf(const gpu::CompiledShader& shader)
		{
			std::vector<Parameters::Param> params;
			for (const gpu::ShaderParamField& field : shader.params)
			{
				Parameters::Param param;
				param.name = field.name;
				param.label = field.label;
				param.defaultValue = field.defaultValue;
				param.minValue = field.minValue;
				param.maxValue = field.maxValue;
				param.automatable = true;
				params.push_back(std::move(param));
			}
			return params;
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

		if (shaderParamsPending)
			_applyPendingShaderParams();
	}

	void ReaShaderPlugin::onMainThread()
	{
		if (shaderParamsPending)
		{
			if (!active)
				_applyPendingShaderParams();
			else if (!restartRequested)
			{
				restartRequested = true;
				host->request_restart(host); // -> deactivate() -> activate()
			}
		}

		if (hostChangedParams.exchange(false))
		{
			for (const Parameters::Param& p : params.list())
			{
				if (p.automatable)
					_webuiSend({ { "type", "paramValue" }, { "id", p.id }, { "value", params.value(p.id) } });
			}
		}
	}

	// -------- clap.params --------

	uint32_t ReaShaderPlugin::automatableParamCount() const
	{
		return params.automatableCount();
	}

	bool ReaShaderPlugin::getAutomatableParamInfo(uint32_t index, clap_param_info_t* info) const
	{
		auto param = params.automatableAt(index);
		if (!param)
			return false;

		*info = {};
		info->id = param->id;
		info->flags = CLAP_PARAM_IS_AUTOMATABLE;
		std::snprintf(info->name, sizeof(info->name), "%s", param->label.c_str());
		info->min_value = param->minValue;
		info->max_value = param->maxValue;
		info->default_value = param->defaultValue;
		return true;
	}

	bool ReaShaderPlugin::getParamValue(clap_id id, double* value) const
	{
		if (!params.contains(id))
			return false;
		*value = params.value(id);
		return true;
	}

	bool ReaShaderPlugin::valueToText(clap_id, double value, char* buffer, uint32_t size) const
	{
		std::snprintf(buffer, size, "%.3f", value);
		return true;
	}

	bool ReaShaderPlugin::textToValue(clap_id, const char* text, double* value) const
	{
		char* end = nullptr;
		double parsed = std::strtod(text, &end);
		if (end == text)
			return false;
		*value = parsed;
		return true;
	}

	void ReaShaderPlugin::applyHostParamValue(clap_id id, double value)
	{
		if (!params.contains(id))
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
	// { "version": 3, "params": { "<name>": value }, "device": n, "logo": false,
	//   "shader": { "name": "", "compiled": {...} }, "lut": { "name": "", "mode": "after", "data": {...} } }
	// The compiled shader and the LUT are embedded, so a project doesn't depend on the plugin's folders.

	constexpr int kStateVersion = 3;

	bool ReaShaderPlugin::saveState(const clap_ostream_t* stream)
	{
		json state;
		{
			std::lock_guard lock(stateMutex);
			json compiled = shaderData.empty() ? json() : json::parse(shaderData, nullptr, false);
			json lut = lutData.empty() ? json() : json::parse(lutData, nullptr, false);
			state = { { "version", kStateVersion },
					  { "params", params.valuesToJson() },
					  { "device", renderingDevice },
					  { "logo", showLogo },
					  { "shader", { { "name", shaderName }, { "compiled", compiled } } },
					  { "lut", { { "name", lutName }, { "mode", lutMode }, { "data", lut } } } };
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
		if (!state.is_object() || state.value("version", 0) != kStateVersion)
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
			savedShaderValues.clear();
			for (const auto& [name, value] : savedParams.items())
			{
				if (value.is_number())
					savedShaderValues[name] = value.get<double>();
			}
		}

		if (deviceChanged)
			reaShaderRenderer->changeRenderingDevice(device); // no-op when not active
		reaShaderRenderer->setLogoEnabled(logo);

		const json shader = state.value("shader", json::object());
		const json compiled = shader.value("compiled", json());
		if (compiled.is_object())
			_useShader(shader.value("name", ""), compiled.dump());
		else
			_clearShader();

		const json lut = state.value("lut", json::object());
		_setLutMode(lut.value("mode", "after"));
		const json lutStored = lut.value("data", json());
		if (lutStored.is_object())
			_useLut(lut.value("name", ""), lutStored.dump());
		else
			_clearLut();

		_webuiSendSnapshot();
		return true;
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

		// param values at video time, by index in the param list: parmlist[0] is wet/dry, then the param at
		// index i is at parmlist[i + 1] (params REAPER doesn't know yet fall back to the current value)
		double paramValues[Parameters::ParamList::maxCount];
		// while the shader's params wait for a restart, its sliders use their defaults
		size_t paramCount = plugin->shaderParamsPending ? Parameters::DefaultCount : plugin->params.count();
		for (size_t i = 0; i < paramCount; i++)
			paramValues[i] = (int)i + 1 < nparms ? parmlist[i + 1] : plugin->params.valueAt(i);

		FrameView inputFrame{ w, h, input->get_rowspan(), reinterpret_cast<uint8_t*>(input->get_bits()) };
		FrameView outputFrame{ w, h, output->get_rowspan(), reinterpret_cast<uint8_t*>(output->get_bits()) };
		ReaShaderRenderer::FrameInputs inputs{ projectTime, frameRate, paramValues, paramCount };

		// false: inactive, busy, failed, or no shader, LUT or logo -> pass the input through
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
		if (idx < 0 || (size_t)idx >= plugin->params.count())
			return false;
		*valueOut = plugin->params.valueAt((size_t)idx);
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

	void ReaShaderPlugin::_setShaderParams(std::vector<Parameters::Param> shaderParams)
	{
		{
			std::lock_guard lock(stateMutex);
			pendingShaderParams = std::move(shaderParams);
		}
		shaderParamsPending = true;
		host->request_callback(host); // -> onMainThread()
	}

	// main thread, plugin deactivated
	void ReaShaderPlugin::_applyPendingShaderParams()
	{
		std::vector<Parameters::Param> shaderParams;
		Parameters::ValueMap savedValues;
		{
			std::lock_guard lock(stateMutex);
			shaderParams = std::move(pendingShaderParams);
			savedValues = savedShaderValues;
		}
		params.replaceShaderParams(std::move(shaderParams), savedValues);
		shaderParamsPending = false;
		restartRequested = false;

		auto* hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
		if (hostParams)
			hostParams->rescan(host, CLAP_PARAM_RESCAN_ALL);

		_webuiSendSnapshot();
	}

	// -------- shaders --------
	//
	// A shader is compiled once, when uploaded, and stored in the compiled shaders folder as <name>.json.
	// Selecting one loads the stored form; the current one is also embedded in the project state.

	// compiles GLSL, stores it, then uses it
	void ReaShaderPlugin::_uploadShader(const std::string& fileName, const std::string& source)
	{
		std::string name = std::filesystem::path(fileName).stem().string();
		if (name.empty())
			name = "shader";

		std::string data;
		try
		{
			data = gpu::toJson(gpu::compileShader(source, fileName)).dump();
			writeFile(storedPath(util::paths::compiledShadersDir(), name), data);
		}
		catch (const std::exception& e)
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Shader upload failed", e.what());
			_webuiSendShaderStatus(e.what(), "error");
			return;
		}
		_useShader(name, data);
	}

	// makes a compiled shader (its stored JSON) the current one; on error the current one stays
	void ReaShaderPlugin::_useShader(const std::string& name, const std::string& data)
	{
		std::lock_guard chainLock(chainMutex);
		std::shared_ptr<const gpu::CompiledShader> compiled;
		std::string error;
		try
		{
			compiled = std::make_shared<const gpu::CompiledShader>(gpu::fromJson(json::parse(data)));
		}
		catch (const std::exception& e)
		{
			error = std::string("Invalid compiled shader: ") + e.what();
		}
		if (compiled)
		{
			std::shared_ptr<const gpu::LutData> currentLut;
			std::string mode;
			{
				std::lock_guard lock(stateMutex);
				currentLut = lutTable;
				mode = lutMode;
			}
			error = _setChain(compiled, currentLut, mode);
		}

		if (!error.empty())
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Shader load failed", error);
			_webuiSendShaderStatus(error, "error");
			return;
		}

		{
			std::lock_guard lock(stateMutex);
			shaderName = name;
			shaderData = data;
			compiledShader = compiled;
		}
		_setShaderParams(paramsOf(*compiled));
		_webuiSendShaderStatus("Loaded " + name, "ok");
		_webuiSendSnapshot();
	}

	// no shader: video passes through
	void ReaShaderPlugin::_clearShader()
	{
		{
			std::lock_guard chainLock(chainMutex);
			std::shared_ptr<const gpu::LutData> currentLut;
			std::string mode;
			{
				std::lock_guard lock(stateMutex);
				currentLut = lutTable;
				mode = lutMode;
			}
			// only the LUT's node is left, which the renderer already has: nothing to build, nothing can fail
			_setChain(nullptr, currentLut, mode);
			std::lock_guard lock(stateMutex);
			shaderName.clear();
			shaderData.clear();
			compiledShader.reset();
		}
		_setShaderParams({});
		_webuiSendSnapshot();
	}

	// -------- LUT --------
	//
	// A .cube file is parsed once, when uploaded, and stored in the LUTs folder as <name>.json.
	// Selecting one loads the stored form; the current one is also embedded in the project state.

	// parses a .cube file, stores it, then uses it
	void ReaShaderPlugin::_uploadLut(const std::string& fileName, const std::string& source)
	{
		std::string name = std::filesystem::path(fileName).stem().string();
		if (name.empty())
			name = "lut";

		std::string data;
		try
		{
			data = gpu::toJson(gpu::parseCube(source, fileName)).dump();
			writeFile(storedPath(util::paths::lutsDir(), name), data);
		}
		catch (const std::exception& e)
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "LUT upload failed", e.what());
			_webuiSendLutStatus(e.what(), "error");
			return;
		}
		_useLut(name, data);
	}

	// makes a stored LUT (its JSON) the current one; on error the current one stays
	void ReaShaderPlugin::_useLut(const std::string& name, const std::string& data)
	{
		std::lock_guard chainLock(chainMutex);
		std::shared_ptr<const gpu::LutData> parsed;
		std::string error;
		try
		{
			parsed = std::make_shared<const gpu::LutData>(gpu::lutFromJson(json::parse(data)));
		}
		catch (const std::exception& e)
		{
			error = std::string("Invalid stored LUT: ") + e.what();
		}
		if (parsed)
		{
			std::shared_ptr<const gpu::CompiledShader> currentShader;
			std::string mode;
			{
				std::lock_guard lock(stateMutex);
				currentShader = compiledShader;
				mode = lutMode;
			}
			error = _setChain(currentShader, parsed, mode);
		}

		if (!error.empty())
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "LUT load failed", error);
			_webuiSendLutStatus(error, "error");
			return;
		}

		{
			std::lock_guard lock(stateMutex);
			lutName = name;
			lutData = data;
			lutTable = parsed;
		}
		_webuiSendLutStatus("Loaded " + name, "ok");
		_webuiSendSnapshot();
	}

	void ReaShaderPlugin::_clearLut()
	{
		{
			std::lock_guard chainLock(chainMutex);
			std::shared_ptr<const gpu::CompiledShader> currentShader;
			std::string mode;
			{
				std::lock_guard lock(stateMutex);
				currentShader = compiledShader;
				mode = lutMode;
			}
			std::string error = _setChain(currentShader, nullptr, mode);
			if (!error.empty())
			{
				// the shader's iChannel1 goes back to the identity: its pass is kept, so this can't fail in practice
				LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "LUT unload failed", error);
				return;
			}
			std::lock_guard lock(stateMutex);
			lutName.clear();
			lutData.clear();
			lutTable.reset();
		}
		_webuiSendSnapshot();
	}

	// unknown modes are ignored; on error (the shader's own LUT couldn't be built) the mode stays
	void ReaShaderPlugin::_setLutMode(const std::string& mode)
	{
		if (!isLutMode(mode))
			return;
		std::lock_guard chainLock(chainMutex);
		std::shared_ptr<const gpu::CompiledShader> currentShader;
		std::shared_ptr<const gpu::LutData> currentLut;
		{
			std::lock_guard lock(stateMutex);
			currentShader = compiledShader;
			currentLut = lutTable;
		}
		std::string error = _setChain(currentShader, currentLut, mode);
		if (!error.empty())
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "LUT mode change failed", error);
			_webuiSendLutStatus(error, "error");
			return;
		}
		std::lock_guard lock(stateMutex);
		lutMode = mode;
	}

	// The chain for one shader and one LUT, as fixed nodes:
	// - "before": LUT -> shader
	// - "after": shader -> LUT
	// - "shader": the shader alone, sampling the LUT as iChannel1 (with no shader: the LUT alone)
	std::string ReaShaderPlugin::_setChain(std::shared_ptr<const gpu::CompiledShader> chainShader,
										   std::shared_ptr<const gpu::LutData> chainLut, const std::string& mode)
	{
		using Node = ReaShaderRenderer::ChainNode;
		std::vector<Node> chain;
		Node lutNode{ .uid = Parameters::kLutNode, .lut = chainLut, .firstParam = Parameters::LutMixIndex, .paramCount = 1 };
		bool lutInShader = chainShader && mode == "shader";

		if (chainLut && !lutInShader && mode == "before")
			chain.push_back(lutNode);
		if (chainShader)
		{
			size_t paramCount = chainShader->params.size();
			chain.push_back({ .uid = Parameters::kShaderNode,
							  .shader = chainShader,
							  .shaderLut = lutInShader ? chainLut : nullptr,
							  .firstParam = Parameters::DefaultCount,
							  .paramCount = paramCount < Parameters::kNodeSlots ? paramCount : Parameters::kNodeSlots });
		}
		if (chainLut && !lutInShader && mode != "before")
			chain.push_back(lutNode);

		return reaShaderRenderer->setChain(std::move(chain));
	}

	// -------- web UI --------
	//
	// Messages to the UI:
	// - snapshot    { version, track, params, devices, logo, shader, shaders, lut, luts }: everything, the UI
	//                rebuilds itself from it
	// - paramValue  { id, value }: a host automation change
	// - shaderStatus{ status, state }: state is "busy", "ok" or "error"
	// - lutStatus   { status, state }: the same, for the LUT
	//
	// Messages from the UI:
	// - ready       {}: the page loaded, send a snapshot
	// - paramValue  { id, value }
	// - renderingDevice { index }
	// - logo        { enabled }: the spinning logo over the video (shown with the about box)
	// - openUrl     { url }: opens an https:// link in the system browser
	// - shaderSelect{ name }: a compiled shader, "" = none
	// - shaderUpload{ name, source }: GLSL to compile and store
	// - lutSelect   { name }: a stored LUT, "" = none
	// - lutUpload   { name, source }: a .cube file's text, to parse and store
	// - lutMode     { mode }: "before" / "after" the shader, or "shader" (the shader samples it as iChannel1)

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

			msg = { { "type", "snapshot" },
					{ "version", REASHADER_VERSION },
					{ "track", { { "number", trackNumber }, { "name", track } } },
					{ "devices", { { "names", renderingDeviceNames }, { "selected", renderingDevice } } },
					{ "logo", showLogo },
					{ "shader", { { "name", shaderName } } },
					{ "lut", { { "name", lutName }, { "mode", lutMode } } } };
		}
		msg["params"] = params.toJson();
		msg["shaders"] = storedNames(util::paths::compiledShadersDir());
		msg["luts"] = storedNames(util::paths::lutsDir());

		_webuiSend(msg);
	}

	void ReaShaderPlugin::_webuiSendShaderStatus(const std::string& status, const char* state)
	{
		_webuiSend({ { "type", "shaderStatus" }, { "status", status }, { "state", state } });
	}

	void ReaShaderPlugin::_webuiSendLutStatus(const std::string& status, const char* state)
	{
		_webuiSend({ { "type", "lutStatus" }, { "status", status }, { "state", state } });
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

			params.setValue(param->id, msg.value("value", param->defaultValue));
			if (param->automatable)
			{
				params.flagForHost(param->id);
				auto* hostParams = static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
				if (hostParams)
					hostParams->request_flush(host);
			}
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
		else if (type == "shaderSelect")
		{
			std::string name = std::filesystem::path(msg.value("name", "")).filename().string(); // no paths from the UI
			if (name.empty())
			{
				_clearShader();
				_webuiSendShaderStatus("No shader: video passes through", "ok");
				return;
			}
			std::string data = readFile(storedPath(util::paths::compiledShadersDir(), name));
			if (data.empty())
				_webuiSendShaderStatus("Can't read the compiled shader " + name, "error");
			else
				_useShader(name, data);
		}
		else if (type == "shaderUpload")
		{
			_uploadShader(msg.value("name", ""), msg.value("source", ""));
		}
		else if (type == "lutSelect")
		{
			std::string name = std::filesystem::path(msg.value("name", "")).filename().string(); // no paths from the UI
			if (name.empty())
			{
				_clearLut();
				_webuiSendLutStatus("No LUT", "ok");
				return;
			}
			std::string data = readFile(storedPath(util::paths::lutsDir(), name));
			if (data.empty())
				_webuiSendLutStatus("Can't read the stored LUT " + name, "error");
			else
				_useLut(name, data);
		}
		else if (type == "lutUpload")
		{
			_uploadLut(msg.value("name", ""), msg.value("source", ""));
		}
		else if (type == "lutMode")
		{
			_setLutMode(msg.value("mode", ""));
		}
		else
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Unexpected message from web UI", text);
		}
	}
} // namespace ReaShader
