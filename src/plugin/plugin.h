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

#include "plugin/params.h"

class IREAPERVideoProcessor;
class IVideoFrame;

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

		// acquires the REAPER video tap + track info, starts the renderer the first time
		// (the GPU stays up until the plugin is destroyed, so re-activation is quick)
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

		// -------- renderer support --------

		int getRenderingDeviceIndex() const;
		void setRenderingDeviceIndex(int index);
		void setRenderingDevicesList(const std::vector<std::string>& deviceNames);

		// Replaces the params reflected from the current shader (any thread).
		// The host's param list may only change while deactivated: when active, the plugin asks the
		// host to restart it and swaps the params in deactivate(), then asks the host to rescan.
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

	  private:
		// REAPER video processor callbacks (video thread); userdata is the plugin
		static IVideoFrame* _processVideoFrame(IREAPERVideoProcessor* videoProcessor, const double* parmlist,
											   int nparms, double projectTime, double frameRate, int forceFormat);
		static bool _getVideoParam(IREAPERVideoProcessor* videoProcessor, int idx, double* valueOut);

		void _uploadShader(const std::string& fileName, const std::string& source);
		void _useShader(const std::string& name, const std::string& data);
		void _clearShader();
		void _applyPendingShaderParams();

		void _webuiSend(const Parameters::json& msg);
		void _webuiSendSnapshot();
		void _webuiSendShaderStatus(const std::string& status, const char* state);

		const clap_host_t* host{ nullptr };
		std::unique_ptr<ReaShaderRenderer> reaShaderRenderer;
		IREAPERVideoProcessor* videoProcessor{ nullptr };

		Parameters::ParamList params;
		std::atomic<bool> hostChangedParams{ false }; // echo to the web UI on the main thread
		std::atomic<bool> active{ false };
		std::atomic<bool> shaderParamsPending{ false }; // the shader's params wait for a restart
		bool restartRequested{ false };					 // main thread only

		// everything below is guarded by stateMutex
		mutable std::mutex stateMutex;
		int renderingDevice{ 0 };
		std::vector<std::string> renderingDeviceNames;
		std::string shaderName; // empty = no shader
		std::string shaderData; // the current shader's compiled form (JSON)
		Parameters::ValueMap savedShaderValues; // restored when the shader's params appear
		std::vector<Parameters::Param> pendingShaderParams;
		int trackNumber{ 0 };					// 1-based, 0 = not found, -1 = master
		std::string trackName;

		std::mutex webUISenderMutex;
		WebUISender webUISender;
	};
} // namespace ReaShader
