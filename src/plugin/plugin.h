/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <clap/clap.h>

#include "rsparams/params.h"

#include "wdltypes.h" // video_frame.h needs WDL_FIXALIGN/INT_PTR but doesn't include this itself
#include "video_processor.h"

namespace ReaShader
{
	class ReaShaderRenderer; // forward-declared to keep Vulkan headers out of this header

	// The plugin's logic, one instance per plugin instance: parameters, state, web UI messages and
	// the REAPER video tap. The CLAP shell (src/clap) forwards everything here.
	//
	// Threads:
	// - main:    CLAP lifecycle, state, onMainThread()
	// - audio:   process()/flush() -> host param events, lock-free param values only
	// - video:   REAPER's video callbacks -> renderer (try_lock, never waits)
	// - webview: handleWebUIMessage() -> param edits, device switch, shader upload
	class ReaShaderPlugin
	{
	  public:
		ReaShaderPlugin();
		~ReaShaderPlugin();

		void initialize(const clap_host_t* host);

		// acquires the REAPER video tap + track info, starts the renderer; clap_plugin_t::activate
		void activate();
		void deactivate();

		// host callback requested by this plugin (clap_plugin_t::on_main_thread)
		void onMainThread();

		// -------- clap.params --------

		uint32_t automatableParamCount() const;
		bool getAutomatableParamInfo(uint32_t index, clap_param_info_t* info) const;
		bool getParamValue(clap_id id, double* value) const;
		bool valueToText(clap_id id, double value, char* buffer, uint32_t size) const;
		bool textToValue(clap_id id, const char* text, double* value) const;

		// host automation / host UI edit (audio thread): lock-free, echoed to the web UI later
		void applyHostParamValue(clap_id id, double value);
		// web UI edits to forward to the host (audio thread), false when none are left
		bool takeParamChangeForHost(clap_id& id, double& value);

		// -------- clap.state --------

		bool saveState(const clap_ostream_t* stream);
		bool loadState(const clap_istream_t* stream);

		// -------- REAPER video tap --------

		bool getVideoTapParamValue(int idx, double* valueOut) const;

		// -------- renderer support --------

		int getRenderingDeviceIndex() const;
		void setRenderingDeviceIndex(int index);
		void setRenderingDevicesList(const std::vector<std::string>& deviceNames);

		// replaces the params reflected from the current shader
		void setShaderParams(std::vector<Parameters::Param> shaderParams);

		// -------- web UI --------

		// WebUIHost registers a sender when its webview is ready and clears it on teardown;
		// messages sent while no sender is registered are dropped.
		using WebUISender = std::function<void(const std::string&)>;
		void setWebUISender(WebUISender sender);
		void clearWebUISender();

		// entry point for a JSON message coming from the web UI
		void handleWebUIMessage(const std::string& msg);

		double getAudioGain() const;

		// public because the REAPER video callback (processFrame in reashader_clap.cpp) drives it
		std::unique_ptr<ReaShaderRenderer> reaShaderRenderer;

	  private:
		void _applyShader();

		void _webuiSend(const Parameters::json& msg);
		void _webuiSendSnapshot();
		void _webuiSendShaderStatus(const std::string& status, bool error);

		const clap_host_t* host{ nullptr };

		Parameters::ParamList params;
		std::atomic<bool> hostChangedParams{ false }; // echo to the web UI on the main thread

		// everything below is guarded by stateMutex
		mutable std::mutex stateMutex;
		int renderingDevice{ 0 };
		std::vector<std::string> renderingDeviceNames;
		std::string shaderName;
		std::string shaderSource;
		Parameters::json savedShaderValues = Parameters::json::object(); // restored when the shader's params appear
		int trackNumber{ 0 };											  // 1-based, 0 = not found, -1 = master
		std::string trackName;

		std::mutex webUISenderMutex;
		WebUISender webUISender;

		IREAPERVideoProcessor* videoProcessor{ nullptr };
	};

	// REAPER video processor callbacks, defined in reashader_clap.cpp
	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
								   double project_time, double frate, int force_format);
	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut);
} // namespace ReaShader
