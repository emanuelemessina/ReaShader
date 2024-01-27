/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "tools/fwd_decl.h"

#include "video_processor.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <base/source/fstreamer.h>
#include <pluginterfaces/base/funknown.h>
#include <pluginterfaces/vst/vsttypes.h>

using namespace Steinberg;

#include "rsrenderer.h"
#include "rsui/api.h"

#include <mutex>

namespace ReaShader
{
	FWD_DECL(MyPluginProcessor);

	// reaper video processor functions
	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
								   double project_time, double frate, int force_format);
	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut);

	class ReaShaderProcessor
	{
	  public:
		ReaShaderProcessor(MyPluginProcessor* processor, FUnknown* context);

		void initialize();
		/**
		 * @brief Gets called when the controller has relayed a json message from the web ui
		 * @param msg
		 */
		void receivedJSONFromController(json msg);
		void receivedBinaryFromController(const char* data, size_t size);
		void receivedFileFromController(json&& info, std::vector<char>&& data);

		/**
		 * @brief Gets called from the plugin processor when a parameter is updated from the DAW
		 * @param id
		 * @param newValue
		 */
		void vstParameterUpdate(Vst::ParamID id, Vst::ParamValue newValue);
		void storeParamsValues(IBStream* state);
		void loadParamsValues(IBStream* state);
		void activate();
		void deactivate();
		void terminate();

		void setRenderingDevicesList(std::vector<VkPhysicalDeviceProperties>&);

		std::mutex rsparamsVectorMutex; // for locking during loadState (and halt the processing)
		std::vector<std::unique_ptr<Parameters::IParameter>> processor_rsParams;
		std::unique_ptr<ReaShaderRenderer> reaShaderRenderer;

	  protected:
		MyPluginProcessor* myPluginProcessor;
		FUnknown* context;

		// bind reaper processvideoframe to reashaderprocessor class
		friend IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
											  double project_time, double frate, int force_format);

	  private:

		template <typename P, typename... Args> void _registerParam(Args&&... args);
		void _registerDefaultParams();

		/**
		 * @brief Message to controller gets relayed to web ui
		 * @param msg
		 */
		void _sendJSONToController(json msg);

		void _webuiSendVSTParamUpdate(Vst::ParamID id, Vst::ParamValue newValue);
		void _webuiSendTrackInfo();
		void _webuiSendRenderingDevicesList();

		std::vector<VkPhysicalDeviceProperties> renderingDevicesList;
		ReaShader::TrackInfo trackInfo{-1};

		int myColor{ 0 };

		IREAPERVideoProcessor* m_videoproc{ nullptr };
		
	};
} // namespace ReaShader