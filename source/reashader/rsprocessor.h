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
		void receivedJSONfromUI(json msg);
		void parameterUpdate(Vst::ParamID id, Vst::ParamValue newValue);
		void storeParamsValues(IBStream* state);
		void loadParamsValues(IBStream* state);
		void activate();
		void deactivate();
		void terminate();

	  protected:
		MyPluginProcessor* myPluginProcessor;
		FUnknown* context;

		// bind reaper processvideoframe to reashaderprocessor class
		friend IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
											  double project_time, double frate, int force_format);

		std::unique_ptr<ReaShaderRenderer> reaShaderRenderer;

	  private:
		int myColor;

		IREAPERVideoProcessor* m_videoproc{ nullptr };
	};
} // namespace ReaShader