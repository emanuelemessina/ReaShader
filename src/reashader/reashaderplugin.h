/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <clap/clap.h>

#include "rsparams/rsparams.h"
#include "rsui/api.h"

#include "wdltypes.h" // video_frame.h needs WDL_FIXALIGN/INT_PTR but doesn't include this itself
#include "video_processor.h"


namespace ReaShader
{
	class ReaShaderRenderer; // forward-declared to keep Vulkan headers out of this header

	// The plugin's logic, one instance per plugin instance: the parameter list, state, web UI
	// messages and the REAPER video tap. The CLAP shell (src/clap) forwards everything here.
	class ReaShaderPlugin
	{
	  public:
		ReaShaderPlugin();
		~ReaShaderPlugin();

		void initialize();
		void terminate();

		// acquires the REAPER video tap + track info; called from clap_plugin_t::activate
		void activate(const clap_host_t* host);
		void deactivate();

		// -------- clap.params support --------

		// the NumericParameter and Int8u entries of rsParams; String params are UI/state only
		uint32_t automatableParamCount() const;
		bool getAutomatableParamInfo(uint32_t index, clap_param_info_t* info) const;
		bool getParamValue(clap_id id, double* value) const;
		bool valueToText(clap_id id, double value, char* buffer, uint32_t size) const;
		bool textToValue(clap_id id, const char* text, double* value) const;

		// applies a value change coming from host automation / the host's own generic param UI,
		// and mirrors it to the web UI. Does not re-notify the host (it already knows).
		void applyHostParamValue(clap_id id, double value);

		// drains one pending param change that originated from the web UI, so the CLAP shell can
		// forward it to the host as an outgoing CLAP_EVENT_PARAM_VALUE event. Returns false when
		// there's nothing pending.
		bool popPendingHostNotification(clap_id& id, double& value);

		// -------- clap.state support --------

		void saveState(const clap_ostream_t* stream);
		void loadState(const clap_istream_t* stream);

		// -------- REAPER video tap --------

		// value of parameter idx as seen by REAPER's video thread (NumericParameter only)
		bool getVideoTapParamValue(int idx, double* valueOut) const;

		// -------- renderer support (mutex-guarded) --------

		uint8_t getRenderingDeviceIndex() const;
		void setRenderingDeviceIndex(uint8_t index);

		// stores the GPU names and refreshes the web UI's device list
		void setRenderingDevicesList(const std::vector<std::string>& deviceNames);

		size_t rsParamsCount() const;

		// appends a renderer-created parameter (e.g. a custom shader uniform) and notifies the web UI
		void addRendererParam(std::unique_ptr<Parameters::IParameter>& param);

		// -------- web UI --------

		// WebUIHost registers a sender when its webview is ready and clears it on teardown;
		// messages sent while no sender is registered are dropped.
		using WebUISender = std::function<void(const std::string&)>;
		void setWebUISender(WebUISender sender);
		void clearWebUISender();

		// entry point for a JSON message coming from the web UI (called by the GUI's bind() callback)
		void handleWebUIMessage(const std::string& msg);

		double getAudioGain() const;

	  private:
		void _registerDefaultParams();
		template <typename P, typename... Args> void _registerParam(Args&&... args);

		void _receivedFileFromWebUI(json&& metadata, std::string&& name, std::string&& extension, size_t size,
									 std::vector<char>&& data);

		void _webuiSendParamUpdate(Parameters::Id id, double newValue);
		void _webuiSendTrackInfo();
		void _webuiSendRenderingDevicesList();
		void _webuiSend(json msg);

		void _queueHostNotification(clap_id id, double value);

		mutable std::mutex rsParamsMutex; // guards rsParams across host automation / webui edits / state load
		std::vector<std::unique_ptr<Parameters::IParameter>> rsParams;

	  public:
		// public because the REAPER video callback (processFrame in reashader_clap.cpp) drives it
		std::unique_ptr<ReaShaderRenderer> reaShaderRenderer;

	  private:
		std::mutex _webUISenderMutex; // set/cleared on the UI thread, used from any thread
		WebUISender _webUISender;

		std::mutex _pendingNotificationsMutex;
		std::vector<std::pair<clap_id, double>> _pendingHostNotifications;

		std::vector<std::string> renderingDeviceNames;

		TrackInfo trackInfo{ -1, nullptr };

		IREAPERVideoProcessor* m_videoproc{ nullptr };
	};

	// REAPER video processor callbacks, defined in reashader_clap.cpp
	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
									double project_time, double frate, int force_format);
	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut);
} // namespace ReaShader
