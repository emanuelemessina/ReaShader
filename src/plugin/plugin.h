/**
 * @file
 * @brief ReaShaderPlugin: the plugin behind the CLAP shell.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

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

	namespace gpu
	{
		struct CompiledShader;
		struct LutData;
	} // namespace gpu

	// The plugin's logic, one instance per plugin instance: parameters, state, web UI messages and
	// the REAPER video tap. The CLAP shell (src/clap) forwards everything here.
	//
	// Threads:
	// - main:    CLAP lifecycle, state, onMainThread()
	// - audio:   process()/flush() -> host param events, lock-free param values only
	// - video:   REAPER's video callbacks -> renderer (try_lock, never waits)
	// - webview: handleWebUIMessage() -> param edits, device switch, chain edits, shader and LUT upload
	class ReaShaderPlugin
	{
	  public:
		// One node of the chain: a stored shader or LUT, by name. Its stored JSON is saved in the state, and its
		// parsed form goes to the renderer (a new pointer only when the content changes, so the renderer keeps
		// the GPU objects of nodes that didn't change).
		struct Node
		{
			enum class Kind
			{
				Shader,
				Lut
			};

			uint32_t uid = 0; // 0..kMaxNodes-1, for the node's life; its params' ids and names derive from it
			Kind kind = Kind::Shader;
			std::string name; // the stored file's stem
			std::string data; // the stored JSON
			std::shared_ptr<const gpu::CompiledShader> shader;
			std::shared_ptr<const gpu::LutData> lut;
			bool bypass = false; // left out of the frame; its params stay

			// shader nodes: the LUT the shader samples as iChannel1 (none: an identity)
			std::string lutName;
			std::string lutData;
			std::shared_ptr<const gpu::LutData> shaderLut;
		};

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

		// Replaces the chain nodes' params (any thread).
		// The host's param list may only change while deactivated: when active, the plugin asks the
		// host to restart it and swaps the params in deactivate(), then asks the host to rescan.
		void _setNodeParams(std::vector<Parameters::NodeParams> nodeParams);
		void _applyPendingNodeParams();

		// Runs `edit` on a copy of the chain, hands the result to the renderer, and keeps it on success.
		// `paramsChange`: nodes were added, removed, moved or swapped, so their params are replaced.
		// Returns the error (from `edit` or the renderer), empty on success; on error the chain stays.
		std::string _editChain(const std::function<std::string(std::vector<Node>&)>& edit, bool paramsChange);

		// reports a chain change to the UI: the error, or `done`, then a snapshot
		void _chainEdited(const std::string& error, const std::string& done);

		// compiles GLSL or parses a .cube file, stores it, then appends a node with it
		void _upload(Node::Kind kind, const std::string& fileName, const std::string& source);
		// the chain from a v4 state's "chain" (invalid nodes are skipped)
		void _loadChain(const Parameters::json& chainState);

		void _webuiSend(const Parameters::json& msg);
		void _webuiSendSnapshot();
		void _webuiSendChainStatus(const std::string& status, const char* state);

		const clap_host_t* host{ nullptr };
		std::unique_ptr<ReaShaderRenderer> reaShaderRenderer;
		IREAPERVideoProcessor* videoProcessor{ nullptr };

		Parameters::ParamList params;
		std::atomic<bool> hostChangedParams{ false }; // echo to the web UI on the main thread
		std::atomic<bool> active{ false };
		std::atomic<bool> nodeParamsPending{ false };	 // the nodes' params wait for a restart
		bool restartRequested{ false };					 // main thread only

		// everything below is guarded by stateMutex
		mutable std::mutex stateMutex;
		int renderingDevice{ 0 };
		std::vector<std::string> renderingDeviceNames;
		bool showLogo{ false };
		std::vector<Node> chain;			  // in order
		Parameters::ValueMap savedNodeValues; // from a loaded state, restored when the nodes' params appear
		std::vector<Parameters::NodeParams> pendingNodeParams;
		int trackNumber{ 0 };					// 1-based, 0 = not found, -1 = master
		std::string trackName;

		// serializes changes to the chain, from copying it to the renderer's setChain
		std::mutex chainMutex;

		std::mutex webUISenderMutex;
		WebUISender webUISender;
	};
} // namespace ReaShader
