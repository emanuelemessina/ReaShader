#include "rsprocessor.h"
#include "mypluginprocessor.h"
#include "rsparams.h"

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

	void ReaShaderProcessor::initialize()
	{
		srand(time(NULL));

		myColor = rand() % 0xffffff | (0xff0000);

		reaShaderRenderer = std::make_unique<ReaShaderRenderer>(this);
		reaShaderRenderer->init();
	}

	void ReaShaderProcessor::_sendJSONToController(json msg)
	{
		myPluginProcessor->sendTextMessage(msg.dump().c_str());
	}

	void ReaShaderProcessor::_webuiSendParamUpdate(Vst::ParamID id, Vst::ParamValue newValue)
	{
		json j;
		j["paramId"] = id;
		j["value"] = newValue;
		j["type"] = RSUI::typeStrings[RSUI::ParamUpdate];

		_sendJSONToController(j);
	}
	void ReaShaderProcessor::_webuiSendTrackInfo(ReaShader::TrackInfo trackInfo)
	{
		json j;

		j["type"] = RSUI::typeStrings[RSUI::TrackInfo];

		j["trackNumber"] = trackInfo.number;

		if (trackInfo.number == -1) // master track
			j["trackName"] = "MASTER";
		else if (trackInfo.number == 0)
			j["trackName"] = "Track Not Found";

		if (trackInfo.name)
		{
			j["trackName"] = trackInfo.name;
		}

		_sendJSONToController(j);
	}

	void ReaShaderProcessor::receivedJSONFromController(json msg)
	{
		RSUI::MessageHandler(msg)
			.reacToParamUpdate(
				[&](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue newValue) { rsParams[id].value = newValue; })
			.reactToRequest([&](RSUI::RequestType type) {
				switch (type)
				{
					case RSUI::RequestType::WantTrackInfo:
						_webuiSendTrackInfo(trackInfo);
						break;
				}
			})
			.fallbackWarning("ReaShaderProcessor");
	}

	void ReaShaderProcessor::parameterUpdate(Vst::ParamID id, Vst::ParamValue newValue)
	{
		// update processor param (change was made by vstui or automation)
		rsParams[id].value = newValue;
		// relay back to frontend to update webui
		_webuiSendParamUpdate(id, newValue);
	}
	void ReaShaderProcessor::storeParamsValues(IBStream* state)
	{
		// here we need to save the model
		IBStreamer streamer(state, kLittleEndian);

		// make sure everything is saved in order

		for (int i = 0; i < uNumParams; i++)
		{
			if (streamer.writeFloat(rsParams[i].value) == false)
			{
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderProcessor", "IBStreamer write error",
					std::format("Cannot write param w/ id {}", i));
			}
		}
	}
	void ReaShaderProcessor::loadParamsValues(IBStream* state)
	{
		// called when we load a preset or project, the model has to be reloaded
		IBStreamer streamer(state, kLittleEndian);
		// make sure everything is read in order (in the exact way it was saved)

		for (int i = 0; i < uNumParams; i++)
		{
			float savedParam = 0.f;
			if (streamer.readFloat(savedParam) == false)
			{
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderProcessor", "IBStreamer read error",
					std::format("Cannot read param w/ id {}", i));
			}
			rsParams[i].value = savedParam;
		}
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

				void* (*GetSetMediaTrackInfo)(MediaTrack * tr, const char* parmname, void* setNewValue);
				*(void**)&GetSetMediaTrackInfo = reaperApp->getReaperApi("GetSetMediaTrackInfo");
				if (GetSetMediaTrackInfo)
				{
					// P_NAME : char * : track name (on master returns NULL)
					trackInfo.name = (char*)GetSetMediaTrackInfo(track, "P_NAME", nullptr);
				}

				double (*GetMediaTrackInfo_Value)(MediaTrack * tr, const char* parmname);
				*(void**)&GetMediaTrackInfo_Value = reaperApp->getReaperApi("GetMediaTrackInfo_Value");
				if (GetMediaTrackInfo_Value)
				{
					// IP_TRACKNUMBER : int : track number 1-based, 0=not found, -1=master track (read-only, returns
					// the int directly)
					trackInfo.number = GetMediaTrackInfo_Value(track, "IP_TRACKNUMBER");
				}

				// send track info to ui

				_webuiSendTrackInfo(trackInfo);
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

	//-----------------------------------------

	/* video */

	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut)
	{
		// called from video thread, gets current state

		if (idx >= 0 && idx < uNumParams)
		{
			ReaShaderProcessor* reaShaderProcessor = (ReaShaderProcessor*)vproc->userdata;
			// directly pass parameters to the video processor
			//*valueOut = (*((std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>*)
			//(((int**)vproc->userdata)[0]))).at(idx);
			*valueOut = rsParams[idx].value;

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

			float videoParam = parmlist[uVideoParam + 1];
			double pushConstants[] = { project_time, frate, videoParam };

			rsProcessor->reaShaderRenderer->drawFrame(pushConstants);

			rsProcessor->reaShaderRenderer->transferFrame(bits);
		}

		return vf;
	}
} // namespace ReaShader