/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "plugin/plugin.h"

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
		// compiled shaders: <name>.json, written on upload
		std::filesystem::path compiledShaderPath(const std::string& name)
		{
			return util::paths::compiledShadersDir() / (name + ".json");
		}

		std::vector<std::string> compiledShaders()
		{
			std::vector<std::string> names;
			std::error_code error;
			for (const auto& entry : std::filesystem::directory_iterator(util::paths::compiledShadersDir(), error))
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
		if (id >= params.count())
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
		if (id >= params.count())
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
	// { "version": 2, "params": { "<name>": value }, "device": n, "logo": false, "shader": { "name": "", "compiled": {...} } }
	// The compiled shader is embedded, so a project doesn't depend on the plugin's shader folder.

	bool ReaShaderPlugin::saveState(const clap_ostream_t* stream)
	{
		json state;
		{
			std::lock_guard lock(stateMutex);
			json compiled = shaderData.empty() ? json() : json::parse(shaderData, nullptr, false);
			state = { { "version", 2 },
					  { "params", params.valuesToJson() },
					  { "device", renderingDevice },
					  { "logo", showLogo },
					  { "shader", { { "name", shaderName }, { "compiled", compiled } } } };
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
		if (!state.is_object() || state.value("version", 0) != 2)
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

		// param values at video time: parmlist[0] is wet/dry, then param i is at parmlist[i + 1]
		// (params REAPER doesn't know yet fall back to the current value)
		double paramValues[Parameters::ParamList::maxCount];
		// while the shader's params wait for a restart, its sliders use their defaults
		size_t paramCount = plugin->shaderParamsPending ? Parameters::DefaultCount : plugin->params.count();
		for (size_t id = 0; id < paramCount; id++)
			paramValues[id] = (int)id + 1 < nparms ? parmlist[id + 1] : plugin->params.value((Parameters::Id)id);

		FrameView inputFrame{ w, h, input->get_rowspan(), reinterpret_cast<uint8_t*>(input->get_bits()) };
		FrameView outputFrame{ w, h, output->get_rowspan(), reinterpret_cast<uint8_t*>(output->get_bits()) };
		ReaShaderRenderer::FrameInputs inputs{ projectTime, frameRate, paramValues, paramCount };

		// false: inactive, busy, failed, or no shader and no logo -> pass the input through
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

	void ReaShaderPlugin::setShaderParams(std::vector<Parameters::Param> shaderParams)
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
			writeFile(compiledShaderPath(name), data);
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
		std::string error;
		try
		{
			error = reaShaderRenderer->setShader(gpu::fromJson(json::parse(data)));
		}
		catch (const std::exception& e)
		{
			error = std::string("Invalid compiled shader: ") + e.what();
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
		}
		_webuiSendShaderStatus("Loaded " + name, "ok");
		_webuiSendSnapshot();
	}

	// no shader: video passes through
	void ReaShaderPlugin::_clearShader()
	{
		reaShaderRenderer->clearShader();
		{
			std::lock_guard lock(stateMutex);
			shaderName.clear();
			shaderData.clear();
		}
		_webuiSendSnapshot();
	}

	// -------- web UI --------
	//
	// Messages to the UI:
	// - snapshot    { version, track, params, devices, logo, shader, shaders }: everything, the UI rebuilds itself from it
	// - paramValue  { id, value }: a host automation change
	// - shaderStatus{ status, state }: state is "busy", "ok" or "error"
	//
	// Messages from the UI:
	// - ready       {}: the page loaded, send a snapshot
	// - paramValue  { id, value }
	// - renderingDevice { index }
	// - logo        { enabled }: the spinning logo over the video (shown with the about box)
	// - openUrl     { url }: opens an https:// link in the system browser
	// - shaderSelect{ name }: a compiled shader, "" = none
	// - shaderUpload{ name, source }: GLSL to compile and store

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
					{ "shader", { { "name", shaderName } } } };
		}
		msg["params"] = params.toJson();
		msg["shaders"] = compiledShaders();

		_webuiSend(msg);
	}

	void ReaShaderPlugin::_webuiSendShaderStatus(const std::string& status, const char* state)
	{
		_webuiSend({ { "type", "shaderStatus" }, { "status", status }, { "state", state } });
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
			std::string data = readFile(compiledShaderPath(name));
			if (data.empty())
				_webuiSendShaderStatus("Can't read the compiled shader " + name, "error");
			else
				_useShader(name, data);
		}
		else if (type == "shaderUpload")
		{
			_uploadShader(msg.value("name", ""), msg.value("source", ""));
		}
		else
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Unexpected message from web UI", text);
		}
	}
} // namespace ReaShader
