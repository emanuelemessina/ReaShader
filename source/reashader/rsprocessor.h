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
		/**
		 * @brief Gets called from the plugin processor when a parameter is updated from the DAW
		 * @param id
		 * @param newValue
		 */
		void parameterUpdate(Vst::ParamID id, Vst::ParamValue newValue);
		void storeParamsValues(IBStream* state);
		void loadParamsValues(IBStream* state);
		void activate();
		void deactivate();
		void terminate();

		void setRenderingDevicesList(std::vector<VkPhysicalDeviceProperties>&&);

		std::vector<ReaShaderParameter> processor_rsParams;
		std::unique_ptr<ReaShaderRenderer> reaShaderRenderer;

	  protected:
		MyPluginProcessor* myPluginProcessor;
		FUnknown* context;

		// bind reaper processvideoframe to reashaderprocessor class
		friend IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
											  double project_time, double frate, int force_format);

	  private:
		/**
		 * @brief Message to controller gets relayed to web ui
		 * @param msg
		 */
		void _sendJSONToController(json msg);

		void _webuiSendParamUpdate(Vst::ParamID id, Vst::ParamValue newValue);
		void _webuiSendTrackInfo();
		void _webuiSendRenderingDevicesList();

		std::vector<VkPhysicalDeviceProperties> renderingDevicesList;
		ReaShader::TrackInfo trackInfo;

		int myColor;

		IREAPERVideoProcessor* m_videoproc{ nullptr };
	};
} // namespace ReaShader