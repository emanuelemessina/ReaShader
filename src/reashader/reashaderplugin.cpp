/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "reashaderplugin.h"

#include "reaper_plugin.h"
#include "rsrenderer.h"
#include "tools/base64.h"
#include "tools/logging.h"

#include <cstdio>
#include <cstring>

namespace ReaShader
{
	ReaShaderPlugin::ReaShaderPlugin() = default;
	ReaShaderPlugin::~ReaShaderPlugin() = default;

	template <typename P, typename... Args> void ReaShaderPlugin::_registerParam(Args&&... args)
	{
		rsParams.push_back(std::make_unique<P>(std::forward<Args>(args)...));
	}

	void ReaShaderPlugin::_registerDefaultParams()
	{
		_registerParam<Parameters::NumericParameter>(Parameters::uAudioGain, "Audio Gain", Parameters::Group::Main,
													   "%");
		_registerParam<Parameters::NumericParameter>(Parameters::uVideoParam, "Video Param",
													   Parameters::Group::Main, "%");
		_registerParam<Parameters::Int8u>(Parameters::uRenderingDevice, "Rendering Device",
										   Parameters::Group::RenderingDeviceSelect, 0);
		_registerParam<Parameters::String>(Parameters::uCustomShaderName, "Custom Shader", Parameters::Group::Shader);
	}

	void ReaShaderPlugin::initialize()
	{
		_registerDefaultParams();

		// constructed here but Vulkan isn't touched until activate() calls init() -- matches CLAP's
		// activate/deactivate lifecycle, so a GPU context is only grabbed once actually needed
		reaShaderRenderer = std::make_unique<ReaShaderRenderer>(this);
	}

	void ReaShaderPlugin::terminate()
	{
		reaShaderRenderer.reset();
	}

	void ReaShaderPlugin::activate(const clap_host_t* host)
	{
		auto* reaperInfo =
			static_cast<reaper_plugin_info_t*>(const_cast<void*>(host->get_extension(host, "cockos.reaper_extension")));

		if (!reaperInfo || !reaperInfo->GetFunc)
			return; // not hosted by REAPER (or extension unavailable)

		using ClapGetReaperContext = void* (*)(const clap_host_t*, int);
		using VideoCreateVideoProcessor = IREAPERVideoProcessor* (*)(void*, int);
		using GetSetMediaTrackInfoFn = void* (*)(void* tr, const char* parmname, void* setNewValue);
		using GetMediaTrackInfoValueFn = double (*)(void* tr, const char* parmname);

		auto clapGetReaperContext = (ClapGetReaperContext)reaperInfo->GetFunc("clap_get_reaper_context");
		auto videoCreateVideoProcessor = (VideoCreateVideoProcessor)reaperInfo->GetFunc("video_CreateVideoProcessor");

		if (!clapGetReaperContext)
			return;

		// sel=4 -> "FxDsp": the fxctx video_CreateVideoProcessor wants (see reashader_clap.cpp's header comment)
		void* fxDsp = clapGetReaperContext(host, 4);
		if (fxDsp && videoCreateVideoProcessor)
		{
			m_videoproc = videoCreateVideoProcessor(fxDsp, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION);
			if (m_videoproc)
			{
				m_videoproc->userdata = this;
				m_videoproc->process_frame = processVideoFrame;
				m_videoproc->get_parameter_value = getVideoParam;
			}
		}

		// sel=1 -> parent track
		void* track = clapGetReaperContext(host, 1);
		if (track)
		{
			auto GetSetMediaTrackInfo = (GetSetMediaTrackInfoFn)reaperInfo->GetFunc("GetSetMediaTrackInfo");
			if (GetSetMediaTrackInfo)
			{
				// P_NAME : char * : track name (on master returns NULL)
				trackInfo.name = (const char*)GetSetMediaTrackInfo(track, "P_NAME", nullptr);
			}

			auto GetMediaTrackInfo_Value = (GetMediaTrackInfoValueFn)reaperInfo->GetFunc("GetMediaTrackInfo_Value");
			if (GetMediaTrackInfo_Value)
			{
				// IP_TRACKNUMBER : int : track number 1-based, 0=not found, -1=master track
				trackInfo.number = (int)GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
			}
		}

		if (reaShaderRenderer)
			reaShaderRenderer->init();
	}

	void ReaShaderPlugin::deactivate()
	{
		// stop REAPER's video callbacks first, then tear the renderer down
		if (m_videoproc)
		{
			delete m_videoproc; // MUST delete here, otherwise REAPER keeps calling into a torn-down instance
			m_videoproc = nullptr;
		}

		if (reaShaderRenderer)
			reaShaderRenderer->shutdown();
	}

	// -------- clap.params support --------

	uint32_t ReaShaderPlugin::automatableParamCount() const
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		uint32_t count = 0;
		for (auto& p : rsParams)
			if (p->typeId() == Parameters::Type::NumericParameter || p->typeId() == Parameters::Type::Int8u)
				count++;
		return count;
	}

	bool ReaShaderPlugin::getAutomatableParamInfo(uint32_t index, clap_param_info_t* info) const
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		uint32_t seen = 0;
		for (auto& p : rsParams)
		{
			bool automatable =
				p->typeId() == Parameters::Type::NumericParameter || p->typeId() == Parameters::Type::Int8u;
			if (!automatable)
				continue;

			if (seen == index)
			{
				info->id = p->id;
				info->cookie = nullptr;
				std::snprintf(info->name, sizeof(info->name), "%s", p->title.c_str());
				info->module[0] = '\0';

				if (p->typeId() == Parameters::Type::NumericParameter)
				{
					auto& np = dynamic_cast<Parameters::NumericParameter&>(*p);
					info->min_value = 0.0;
					info->max_value = 1.0;
					info->default_value = np.defaultValue;
					info->flags = np.automatable ? CLAP_PARAM_IS_AUTOMATABLE : 0;
				}
				else // Int8u -- stepped/enum
				{
					auto& iu = dynamic_cast<Parameters::Int8u&>(*p);
					info->min_value = 0.0;
					info->max_value = 255.0;
					info->default_value = 0.0;
					info->flags = CLAP_PARAM_IS_STEPPED;
				}
				return true;
			}
			seen++;
		}
		return false;
	}

	bool ReaShaderPlugin::getParamValue(clap_id id, double* value) const
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		if (id >= rsParams.size())
			return false;

		auto& p = rsParams[id];
		if (p->typeId() == Parameters::Type::NumericParameter)
			*value = dynamic_cast<Parameters::NumericParameter&>(*p).value;
		else if (p->typeId() == Parameters::Type::Int8u)
			*value = (double)dynamic_cast<Parameters::Int8u&>(*p).value;
		else
			return false;

		return true;
	}

	bool ReaShaderPlugin::valueToText(clap_id id, double value, char* buffer, uint32_t size) const
	{
		std::snprintf(buffer, size, "%.3f", value);
		return true;
	}

	bool ReaShaderPlugin::textToValue(clap_id id, const char* text, double* value) const
	{
		char* end = nullptr;
		double v = std::strtod(text, &end);
		if (end == text)
			return false;
		*value = v;
		return true;
	}

	void ReaShaderPlugin::applyHostParamValue(clap_id id, double value)
	{
		{
			std::lock_guard<std::mutex> lock(rsParamsMutex);
			if (id >= rsParams.size())
				return;

			auto& p = rsParams[id];
			if (p->typeId() == Parameters::Type::NumericParameter)
				dynamic_cast<Parameters::NumericParameter&>(*p).value = value;
			else if (p->typeId() == Parameters::Type::Int8u)
				dynamic_cast<Parameters::Int8u&>(*p).value = (uint8_t)value;
			else
				return;
		}

		_webuiSendParamUpdate((Parameters::Id)id, value);
	}

	void ReaShaderPlugin::_queueHostNotification(clap_id id, double value)
	{
		std::lock_guard<std::mutex> lock(_pendingNotificationsMutex);
		_pendingHostNotifications.emplace_back(id, value);
	}

	bool ReaShaderPlugin::popPendingHostNotification(clap_id& id, double& value)
	{
		std::lock_guard<std::mutex> lock(_pendingNotificationsMutex);
		if (_pendingHostNotifications.empty())
			return false;
		std::tie(id, value) = _pendingHostNotifications.front();
		_pendingHostNotifications.erase(_pendingHostNotifications.begin());
		return true;
	}

	// -------- clap.state support --------

	void ReaShaderPlugin::saveState(const clap_ostream_t* stream)
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		Parameters::Preset::write(stream, rsParams, [](std::string&& msg) {
			LOG(WARNING, toConsole | toFile | toBox, "ReaShaderPlugin", "State save error", msg);
		});
	}

	void ReaShaderPlugin::loadState(const clap_istream_t* stream)
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		Parameters::Preset::read(stream, rsParams, [](std::string&& msg) {
			LOG(WARNING, toConsole | toFile | toBox, "ReaShaderPlugin", "State load error", msg);
		});
	}

	// -------- REAPER video tap --------

	bool ReaShaderPlugin::getVideoTapParamValue(int idx, double* valueOut) const
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		if (idx < 0 || idx >= (int)Parameters::uNumDefaultParams || idx >= (int)rsParams.size())
			return false;

		if (rsParams[idx]->typeId() == Parameters::Type::NumericParameter)
			*valueOut = dynamic_cast<Parameters::NumericParameter&>(*rsParams[idx]).value;
		else
			*valueOut = 0;

		return true;
	}

	// -------- renderer support --------

	uint8_t ReaShaderPlugin::getRenderingDeviceIndex() const
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		if (Parameters::uRenderingDevice >= rsParams.size())
			return 0;
		return dynamic_cast<Parameters::Int8u&>(*rsParams[Parameters::uRenderingDevice]).value;
	}

	void ReaShaderPlugin::setRenderingDeviceIndex(uint8_t index)
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		if (Parameters::uRenderingDevice < rsParams.size())
			dynamic_cast<Parameters::Int8u&>(*rsParams[Parameters::uRenderingDevice]).value = index;
	}

	void ReaShaderPlugin::setRenderingDevicesList(const std::vector<std::string>& deviceNames)
	{
		{
			std::lock_guard<std::mutex> lock(rsParamsMutex);
			renderingDeviceNames = deviceNames;
		}
		_webuiSendRenderingDevicesList();
	}

	size_t ReaShaderPlugin::rsParamsCount() const
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		return rsParams.size();
	}

	void ReaShaderPlugin::addRendererParam(std::unique_ptr<Parameters::IParameter>& param)
	{
		_webuiSend(RSUI::MessageBuilder::buildParamAdd(param));
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		rsParams.push_back(std::move(param));
	}

	// -------- web UI --------

	void ReaShaderPlugin::setWebUISender(WebUISender sender)
	{
		std::lock_guard<std::mutex> lock(_webUISenderMutex);
		_webUISender = std::move(sender);
	}

	void ReaShaderPlugin::clearWebUISender()
	{
		std::lock_guard<std::mutex> lock(_webUISenderMutex);
		_webUISender = nullptr;
	}

	double ReaShaderPlugin::getAudioGain() const
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		if (Parameters::uAudioGain >= rsParams.size())
			return 1.0;
		return dynamic_cast<Parameters::NumericParameter&>(*rsParams[Parameters::uAudioGain]).value;
	}

	void ReaShaderPlugin::_webuiSend(json msg)
	{
		WebUISender sender;
		{
			std::lock_guard<std::mutex> lock(_webUISenderMutex);
			sender = _webUISender; // copy out under the lock, invoke outside it
		}
		if (sender)
			sender(msg.dump());
	}

	void ReaShaderPlugin::_webuiSendParamUpdate(Parameters::Id id, double newValue)
	{
		_webuiSend(RSUI::MessageBuilder::buildVSTParamUpdate(id, newValue));
	}

	void ReaShaderPlugin::_webuiSendTrackInfo()
	{
		_webuiSend(RSUI::MessageBuilder::buildTrackInfo(trackInfo));
	}

	void ReaShaderPlugin::_webuiSendRenderingDevicesList()
	{
		std::lock_guard<std::mutex> lock(rsParamsMutex);
		int selected = 0;
		if (Parameters::uRenderingDevice < rsParams.size())
			selected = dynamic_cast<Parameters::Int8u&>(*rsParams[Parameters::uRenderingDevice]).value;
		_webuiSend(RSUI::MessageBuilder::buildRenderingDevicesList(selected, renderingDeviceNames));
	}

	void ReaShaderPlugin::handleWebUIMessage(const std::string& msg)
	{
		RSUI::MessageHandler(msg.c_str())
			.reactToVSTParamUpdate([&](Parameters::Id id, double newValue) {
				{
					std::lock_guard<std::mutex> lock(rsParamsMutex);
					if (id >= rsParams.size())
						return;
					dynamic_cast<Parameters::NumericParameter&>(*rsParams[id]).value = newValue;
				}
				// tell the host so it can reflect this in its own UI / automation
				_queueHostNotification(id, newValue);
			})
			.reactToParamUpdate([&](Parameters::Id id, json data) {
				std::lock_guard<std::mutex> lock(rsParamsMutex);
				if (id < rsParams.size())
					rsParams[id]->setValue(data);
			})
			.reactToRequest([&](RSUI::RequestType type) {
				switch (type)
				{
					case RSUI::RequestType::WantTrackInfo:
						_webuiSendTrackInfo();
						break;
					case RSUI::RequestType::WantRenderingDevicesList:
						_webuiSendRenderingDevicesList();
						break;
					case RSUI::RequestType::WantParamGroupsList:
						_webuiSend(RSUI::MessageBuilder::buildParamGroupsList());
						break;
					case RSUI::RequestType::WantParamTypesList:
						_webuiSend(RSUI::MessageBuilder::buildParamTypesList());
						break;
					case RSUI::RequestType::WantParamsList: {
						std::lock_guard<std::mutex> lock(rsParamsMutex);
						_webuiSend(RSUI::MessageBuilder::buildParamsList(rsParams));
						break;
					}
					default:
						LOG(WARNING, toConsole | toFile, "ReaShaderPlugin", "Unexpected Request type from web UI",
							std::format("Received: {}", msg));
						break;
				}
			})
			.reactToRenderingDeviceChange([&](int newIndex) {
				{
					std::lock_guard<std::mutex> lock(rsParamsMutex);
					if (Parameters::uRenderingDevice < rsParams.size())
						dynamic_cast<Parameters::Int8u&>(*rsParams[Parameters::uRenderingDevice]).value =
							(uint8_t)newIndex;
				}
				if (reaShaderRenderer)
					reaShaderRenderer->changeRenderingDevice(newIndex);
			})
			.reactToParamAdd([&](std::unique_ptr<Parameters::IParameter> newParam) {
				// TODO(renderer pass): dynamically adding a param while the host is active needs
				// CLAP_PARAM_RESCAN_ALL + host->request_restart() for the host to pick it up live;
				// not wired yet -- for now it just becomes visible on the next full rescan.
				std::lock_guard<std::mutex> lock(rsParamsMutex);
				newParam->id = (Parameters::Id)rsParams.size();
				rsParams.push_back(std::move(newParam));
			})
			.reactToFileUpload([&](std::string name, std::string extension, size_t size, json metadata,
									const std::string& base64Data) {
				std::vector<char> data = tools::base64::decode(base64Data);
				_receivedFileFromWebUI(std::move(metadata), std::move(name), std::move(extension), size,
										std::move(data));
			})
			.fallbackWarning("ReaShaderPlugin");
	}

	void ReaShaderPlugin::_receivedFileFromWebUI(json&& metadata, std::string&& name, std::string&& extension,
												  size_t size, std::vector<char>&& data)
	{
		(void)metadata;
		(void)name;
		(void)extension;
		(void)size;

		if (!reaShaderRenderer)
			return;

		reaShaderRenderer->changeCustomShader(
			std::move(data),
			[](std::string&& msg) { LOG(INFO, toConsole | toFile, "ReaShaderPlugin", "Shader upload", msg); },
			[](std::string&& msg) {
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderPlugin", "Shader upload failed", msg);
			},
			[]() { LOG(INFO, toConsole | toFile, "ReaShaderPlugin", "Shader upload", "Finished."); });
	}
} // namespace ReaShader
