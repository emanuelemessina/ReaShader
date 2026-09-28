/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <clap/clap.h>

#include "rsparams/rsparams.h"
#include "rsui/api.h"
#include "rsui/backend/backend.h"

#include "wdltypes.h" // video_frame.h needs WDL_FIXALIGN/INT_PTR but doesn't include this itself
#include "video_processor.h"

namespace ReaShader
{
	// Single unified plugin-logic object: owns the parameter list, the embedded web UI server,
	// and the REAPER video-tap wiring, all directly. Replaces ReaShaderProcessor + ReaShaderController
	// (VST3's forced processor/controller split, and the JSON+IMessage relay that only existed to
	// keep their two separate parameter vectors in sync) -- there's one instance now, so there's
	// nothing to relay: the web UI, the parameter engine, and (eventually) the renderer all read
	// and write the same rsParams vector directly.
	//
	// Does NOT own a ReaShaderRenderer yet -- wiring the real Vulkan pipeline in is a separate,
	// later pass (see CLAUDE.md's migration status). The REAPER video_frame callback still paints
	// the Phase A diagnostic pattern, defined in reashader_clap.cpp.
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

		// rsParams entries exposed as CLAP host parameters (NumericParameter + Int8u types --
		// String params, e.g. the custom shader name, were never host-automatable and stay
		// web UI / state only, same as under VST3).
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

		// -------- REAPER video tap (parameter passthrough only -- see getVideoTapParamValue) --------

		// mirrors the old getVideoParam(): value REAPER's video thread sees for parameter idx.
		// Only NumericParameter-typed default params have a meaningful numeric value here.
		bool getVideoTapParamValue(int idx, double* valueOut) const;

		// -------- web UI --------

		std::string getWebUIUrl() const;

		double getAudioGain() const;

	  private:
		void _registerDefaultParams();
		template <typename P, typename... Args> void _registerParam(Args&&... args);

		void _receivedJSONFromWebUI(const std::string&& msg);
		void _receivedFileFromWebUI(json&& metadata, std::string&& name, std::string&& extension, size_t size,
									 std::vector<char>&& data);
		void _receivedBinaryFromWebUI(const std::vector<char>&& data);

		void _webuiSendParamUpdate(Parameters::Id id, double newValue);
		void _webuiSendTrackInfo();
		void _webuiSendRenderingDevicesList();
		void _webuiSend(json msg);

		void _queueHostNotification(clap_id id, double value);

		mutable std::mutex rsParamsMutex; // guards rsParams across host automation / webui edits / state load
		std::vector<std::unique_ptr<Parameters::IParameter>> rsParams;

	  public:
		// intentionally public: the CLAP shell owns the object but needs direct access for the
		// video-tap callbacks and to construct RSUIServer's callbacks against `this`
		std::unique_ptr<RSUIServer> rsuiServer;

	  private:
		std::mutex _pendingNotificationsMutex;
		std::vector<std::pair<clap_id, double>> _pendingHostNotifications;

		std::vector<std::string> renderingDeviceNames; // empty until the renderer is ported (Phase B+1)

		TrackInfo trackInfo{ -1, nullptr };

		IREAPERVideoProcessor* m_videoproc{ nullptr };
	};

	// REAPER video processor functions (Phase A diagnostic pattern; defined in reashader_clap.cpp)
	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
									double project_time, double frate, int force_format);
	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut);
} // namespace ReaShader
