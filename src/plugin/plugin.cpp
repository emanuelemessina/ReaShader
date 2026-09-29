/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "plugin/plugin.h"

#include "render/renderer.h"
#include "util/logging.h"

#include "reaper_plugin.h"
#include "wdltypes.h" // video_frame.h needs WDL_FIXALIGN/INT_PTR but doesn't include this itself
#include "video_frame.h"
#include "video_processor.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <format>

namespace ReaShader
{
	using Parameters::json;

	ReaShaderPlugin::ReaShaderPlugin() = default;
	ReaShaderPlugin::~ReaShaderPlugin() = default;

	void ReaShaderPlugin::initialize(const clap_host_t* clapHost)
	{
		host = clapHost;
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

		reaShaderRenderer->init();
		_applyShader();
		_webuiSendSnapshot();
	}

	void ReaShaderPlugin::deactivate()
	{
		// stop REAPER's video callbacks before tearing the renderer down
		delete videoProcessor;
		videoProcessor = nullptr;

		reaShaderRenderer->shutdown();
	}

	void ReaShaderPlugin::onMainThread()
	{
		if (!hostChangedParams.exchange(false))
			return;

		for (const Parameters::Param& p : params.list())
		{
			if (p.automatable)
				_webuiSend({ { "type", "paramValue" }, { "id", p.id }, { "value", params.value(p.id) } });
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
		std::snprintf(info->name, sizeof(info->name), "%s", param->name.c_str());
		info->min_value = 0.0;
		info->max_value = 1.0;
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
	// { "version": 1, "params": { "<name>": value }, "device": n, "shader": { "name": "", "source": "" } }

	bool ReaShaderPlugin::saveState(const clap_ostream_t* stream)
	{
		json state;
		{
			std::lock_guard lock(stateMutex);
			state = { { "version", 1 },
					  { "params", params.valuesToJson() },
					  { "device", renderingDevice },
					  { "shader", { { "name", shaderName }, { "source", shaderSource } } } };
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
		if (!state.is_object() || state.value("version", 0) != 1)
		{
			// unknown or pre-JSON state: keep the defaults
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "State load", "Unrecognized state, using defaults");
			return true;
		}

		const json savedParams = state.value("params", json::object());
		params.valuesFromJson(savedParams);

		int device = state.value("device", 0);
		bool deviceChanged;
		{
			std::lock_guard lock(stateMutex);
			deviceChanged = device != renderingDevice;
			renderingDevice = device;
			savedShaderValues.clear();
			for (const auto& [name, value] : savedParams.items())
			{
				if (value.is_number())
					savedShaderValues[name] = value.get<double>();
			}
			const json shader = state.value("shader", json::object());
			shaderName = shader.value("name", "");
			shaderSource = shader.value("source", "");
		}

		if (deviceChanged)
			reaShaderRenderer->changeRenderingDevice(device); // no-op when not active
		_applyShader();
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

		// parmlist[0] is wet/dry; plugin param i is at parmlist[i + 1], valued at video time
		int videoParamIndex = Parameters::VideoParam + 1;
		double videoParam = nparms > videoParamIndex ? parmlist[videoParamIndex] : 0.0;
		double pushConstants[] = { projectTime, frameRate, videoParam };

		// false: renderer inactive, busy or failed -> pass the input through
		if (!plugin->reaShaderRenderer->renderFrame(w, h, reinterpret_cast<int*>(input->get_bits()), pushConstants,
													reinterpret_cast<int*>(output->get_bits())))
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
		Parameters::ValueMap savedValues;
		{
			std::lock_guard lock(stateMutex);
			savedValues = savedShaderValues;
		}
		params.replaceShaderParams(std::move(shaderParams), savedValues);
	}

	void ReaShaderPlugin::_applyShader()
	{
		std::string source;
		{
			std::lock_guard lock(stateMutex);
			source = shaderSource;
		}
		if (source.empty())
			return;

		reaShaderRenderer->changeCustomShader(
			source, [this](const std::string& status) { _webuiSendShaderStatus(status, false); },
			[this](const std::string& error) {
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderPlugin", "Shader compilation failed", error);
				_webuiSendShaderStatus(error, true);
			},
			[this]() { _webuiSendSnapshot(); });
	}

	// -------- web UI --------
	//
	// Messages to the UI:
	// - snapshot    { track, params, devices, shader }: everything, the UI rebuilds itself from it
	// - paramValue  { id, value }: a host automation change
	// - shaderStatus{ status, error }
	//
	// Messages from the UI:
	// - ready       {}: the page loaded, send a snapshot
	// - paramValue  { id, value }
	// - renderingDevice { index }
	// - shaderUpload{ name, source }

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
					{ "track", { { "number", trackNumber }, { "name", track } } },
					{ "devices", { { "names", renderingDeviceNames }, { "selected", renderingDevice } } },
					{ "shader", { { "name", shaderName } } } };
		}
		msg["params"] = params.toJson();

		_webuiSend(msg);
	}

	void ReaShaderPlugin::_webuiSendShaderStatus(const std::string& status, bool error)
	{
		_webuiSend({ { "type", "shaderStatus" }, { "status", status }, { "error", error } });
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
		else if (type == "shaderUpload")
		{
			std::string name = msg.value("name", "");
			std::string source = msg.value("source", "");

			reaShaderRenderer->changeCustomShader(
				source, [this](const std::string& status) { _webuiSendShaderStatus(status, false); },
				[this](const std::string& error) {
					LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Shader compilation failed", error);
					_webuiSendShaderStatus(error, true);
				},
				[this, name, source]() {
					{
						std::lock_guard lock(stateMutex);
						shaderName = name;
						shaderSource = source;
					}
					_webuiSendSnapshot();
				});
		}
		else
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Unexpected message from web UI", text);
		}
	}
} // namespace ReaShader
