/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "rsprocessor.h"
#include "mypluginprocessor.h"
#include "rsparams/rsparams.h"

#include <stdlib.h> /* srand, rand */
#include <time.h>	/* time */

#include "reaper_plugin.h"
// #include "reaper_plugin_functions.h"
#include "reaper_vst3_interfaces.h"

#include "tools/logging.h"

using namespace Steinberg;

namespace ReaShader
{
	ReaShaderProcessor::ReaShaderProcessor(MyPluginProcessor* processor, FUnknown* context)
		: myPluginProcessor(processor), context(context)
	{
	}

	template <typename P, typename... Args>
	void ReaShaderProcessor::_registerParam(Args&&... args)
	{
		processor_rsParams.push_back(std::make_unique<P>(std::forward<Args>(args)...));
	}

	void ReaShaderProcessor::_registerDefaultParams()
	{
		// we need to prepopulate the params with the default vector
		// it's the preocessor duty to initialize and store the first state, that will be also read by the controller
		_registerParam<Parameters::VSTParameter>(Parameters::uAudioGain, "Audio Gain", Parameters::Group::Main, "%");
		_registerParam<Parameters::VSTParameter>(Parameters::uVideoParam, "Video Param", Parameters::Group::Main, "%");
		_registerParam<Parameters::Int8u>(Parameters::uRenderingDevice, "Rendering Device", Parameters::Group::RenderingDeviceSelect, 0);
		_registerParam<Parameters::String>(Parameters::uCustomShaderPath, "Custom Shader", Parameters::Group::Shader);
	}

	void ReaShaderProcessor::initialize()
	{
		srand(time(NULL));

		myColor = rand() % 0xffffff | (0xff0000);

		_registerDefaultParams();

		reaShaderRenderer = std::make_unique<ReaShaderRenderer>(this);
		reaShaderRenderer->init();
	}

	void ReaShaderProcessor::_sendJSONToController(json msg)
	{
		myPluginProcessor->sendTextMessage(msg.dump().c_str());
	}

	void ReaShaderProcessor::_webuiSendVSTParamUpdate(Vst::ParamID id, Vst::ParamValue newValue)
	{
		_sendJSONToController(RSUI::MessageBuilder::buildVSTParamUpdate(id, newValue));
	}
	void ReaShaderProcessor::_webuiSendTrackInfo()
	{
		_sendJSONToController(RSUI::MessageBuilder::buildTrackInfo(trackInfo));
	}

	void ReaShaderProcessor::receivedJSONFromController(json msg)
	{
		RSUI::MessageHandler(msg)
			.reactToVSTParamUpdate([&](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue newValue) {
				dynamic_cast<Parameters::VSTParameter&>(*processor_rsParams[id]).value = newValue;
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
					default:
						LOG(WARNING, toConsole | toFile, "ReaShaderProcessor", "Unexpectd Request type from controller",
							std::format("Received: {}", msg.dump()));
						break;
				}
			})
			.reactToRenderingDeviceChange([&](int newIndex) {
				dynamic_cast<Parameters::Int8u&>(*processor_rsParams[Parameters::uRenderingDevice]).value = (Steinberg::Vst::ParamValue)newIndex;
				reaShaderRenderer->changeRenderingDevice(newIndex);
			})
			.reactToParamAdd([&](std::unique_ptr<Parameters::IParameter> newParam) {
				newParam->id = processor_rsParams.size();
				processor_rsParams.push_back(std::move(newParam));
			})
			.fallbackWarning("ReaShaderProcessor");
	}

	void ReaShaderProcessor::vstParameterUpdate(Vst::ParamID id, Vst::ParamValue newValue)
	{
		// update processor param (change was made by vstui or automation)
		dynamic_cast<Parameters::VSTParameter&>(*processor_rsParams[id]).value = newValue;
		// relay back to frontend to update webui
		_webuiSendVSTParamUpdate(id, newValue);
	}
	// writes the state to daw memory (on start and on exit) and on file (manual save)
	void ReaShaderProcessor::storeParamsValues(IBStream* state)
	{
		Parameters::PresetStreamer streamer{ state };
		streamer.write(processor_rsParams, [&](std::string&& msg) {
			LOG(WARNING, toConsole | toFile | toBox, "ReaShaderProcessor", "RSPresetStreamer write error", std::move(msg));
		});
	}
	// reads the state from daw memory (project state on start) and from file (manual load)
	void ReaShaderProcessor::loadParamsValues(IBStream* state)
	{
		// called when we load a preset or project, the model has to be reloaded
		std::lock_guard<std::mutex> lock(rsparamsVectorMutex); // important!! prevents process function from breaking
		Parameters::PresetStreamer streamer{ state };
		streamer.read(processor_rsParams, [&](std::string&& msg) {
			LOG(WARNING, toConsole | toFile | toBox, "ReaShaderProcessor", "RSPresetStreamer read error", std::move(msg));
		});

		/*
		// old
		IBStreamer streamer(state, kLittleEndian);
		// make sure everything is read in order (in the exact way it was saved)
		for (int i = 0; i < uNumParams; i++)
		{
			float savedParam = 0.f;
			if (streamer.readFloat(savedParam) == false)
			{
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderProcessor", "IBStreamer read error",
					std::format("Cannot read param {} with id {}", processor_rsParams[i].title, i));
			}
			processor_rsParams[i].value = savedParam;
		}
		*/		
	}

	void ReaShaderProcessor::activate()
	{
		// REAPER API (here we are after effOpen equivalent in vst3)

		// query reaper interface
		IReaperHostApplication* reaperApp{ nullptr };
		if (context->queryInterface(IReaperHostApplication_iid, (void**)&reaperApp) == kResultOk)
		{
			// vst2.4: void* ctx = (void*)audioMaster(&cEffect, 0xdeadbeef, 0xdeadf00e, 4, NULL, 0.0f);
			void* ctx = reaperApp->getReaperParent(4);

			if (ctx)
			{
				// get create video processor
				IREAPERVideoProcessor* (*video_CreateVideoProcessor)(void* fxctx, int version){ nullptr };
				// vst2.4: *(void**)&video_CreateVideoProcessor = (void*)audioMaster(&cEffect, 0xdeadbeef,
				// 0xdeadf00d, 0, "video_CreateVideoProcessor", 0.0f);
				*(void**)&video_CreateVideoProcessor = (void*)reaperApp->getReaperApi("video_CreateVideoProcessor");

				if (video_CreateVideoProcessor)
				{
					// create the video processor
					m_videoproc =
						video_CreateVideoProcessor(ctx, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION);
					if (m_videoproc)
					{
						m_videoproc->userdata = this; // pass reashaderprocessor as the data, completes the binding
						m_videoproc->process_frame = processVideoFrame;
						m_videoproc->get_parameter_value = getVideoParam;
					}
				}

				// get track info and send it to ui

				MediaTrack* track = (MediaTrack*)reaperApp->getReaperParent(1);

				void* (*GetSetMediaTrackInfo)(MediaTrack* tr, const char* parmname, void* setNewValue);
				*(void**)&GetSetMediaTrackInfo = reaperApp->getReaperApi("GetSetMediaTrackInfo");
				if (GetSetMediaTrackInfo)
				{
					// P_NAME : char * : track name (on master returns NULL)
					trackInfo.name = (char*)GetSetMediaTrackInfo(track, "P_NAME", nullptr);
				}

				double (*GetMediaTrackInfo_Value)(MediaTrack* tr, const char* parmname);
				*(void**)&GetMediaTrackInfo_Value = reaperApp->getReaperApi("GetMediaTrackInfo_Value");
				if (GetMediaTrackInfo_Value)
				{
					// IP_TRACKNUMBER : int : track number 1-based, 0=not found, -1=master track (read-only, returns
					// the int directly)
					trackInfo.number = GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
				}
			}
		}
	}
	void ReaShaderProcessor::deactivate()
	{
		if (m_videoproc)
			delete m_videoproc; // MUST !! (otherwise continue running processing)
								// also must be here otherwise crash
	}
	void ReaShaderProcessor::terminate()
	{
		reaShaderRenderer->shutdown();
	}

	void ReaShaderProcessor::setRenderingDevicesList(std::vector<VkPhysicalDeviceProperties>& properties)
	{
		renderingDevicesList = properties;
	}

	void ReaShaderProcessor::_webuiSendRenderingDevicesList()
	{
		_sendJSONToController(RSUI::MessageBuilder::buildRenderingDevicesList(
			dynamic_cast<Parameters::Int8u&>(*processor_rsParams[Parameters::uRenderingDevice]).value, renderingDevicesList));
	}

	//-----------------------------------------

	/* video */

	/**
	* @param double* valueOut is a pointer to an array of doubles, reaper's video processor internal representation of the state (set of params). each time a value is assigned the pointer shifts to the next position
	*/
	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut)
	{
		// called from video thread, gets current state

		if (idx >= 0 && idx < Parameters::uNumDefaultParams)
		{
			ReaShaderProcessor* reaShaderProcessor = (ReaShaderProcessor*)vproc->userdata;
			//  directly pass parameters to the video processor
			//*valueOut = (*((std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>*)
			//(((int**)vproc->userdata)[0]))).at(idx);

			// although we have passed, because we can, the entire rsprocessor as additional data to the reaper video processor (thus having access to everything)
			// still because we can, and it's simple and fast, we use also the provided double array state to pass just the indexed values of the vst parameters
			// since we have many parameter types, we check if the type is vst, and if not we simply set the value out to 0
			if (reaShaderProcessor->processor_rsParams[idx]->typeId() == Parameters::Type::VSTParameter)
			{
				*valueOut = dynamic_cast<Parameters::VSTParameter&>(*reaShaderProcessor->processor_rsParams[idx]).value;
			}
			else
			{
				*valueOut = 0;
			}

			return true;
		}
		return false;
	}

	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
								   double project_time, double frate, int force_format)
	{
		//  parmlist[0] is wet, [1] is parameter

		IVideoFrame* vf = vproc->renderInputVideoFrame(0, 'RGBA');

		if (vf)
		{
			int* bits = vf->get_bits();
			// int rowspan = vf->get_rowspan() / 4;
			int w = vf->get_w();
			int h = vf->get_h();
			// int fmt = vf->get_fmt();

			// LICE_WrapperBitmap* bitmap = new LICE_WrapperBitmap((LICE_pixel*)bits, w, h, w, false);

			ReaShaderProcessor* rsProcessor = (ReaShaderProcessor*)vproc->userdata;

			rsProcessor->reaShaderRenderer->checkFrameSize(w, h);

			/*for (int y = 0; y < parmlist[uVideoParam + 1] * h; y++) {
				for (int x = 0; x < w; x++) {
					LICE_PutPixel(bitmap, x, y, rsProcessor->myColor, 0xff, LICE_BLIT_MODE_COPY);
				}
			}*/

			rsProcessor->reaShaderRenderer->loadBitsToImage(bits);

			float videoParam = parmlist[Parameters::uVideoParam + 1];
			double pushConstants[] = { project_time, frate, videoParam };

			rsProcessor->reaShaderRenderer->drawFrame(pushConstants);

			rsProcessor->reaShaderRenderer->transferFrame(bits);
		}

		return vf;
	}
} // namespace ReaShader