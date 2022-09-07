//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#include "mypluginprocessor.h"
#include "myplugincids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "reaper_vst3_interfaces.h"

#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

using namespace Steinberg;

namespace ReaShader {

	//------------------------------------------------------------------------
	// ReaShaderProcessor
	//------------------------------------------------------------------------
	ReaShaderProcessor::ReaShaderProcessor()
	{
		//--- set the wanted controller for our processor
		setControllerClass(kReaShaderControllerUID);

		// Set parameters defaults
		rParams = {
			{ReaShaderParamIds::uAudioGain, 1.f},
			{ReaShaderParamIds::uVideoParam, 0.5f}
		};

		srand(time(NULL));

		myColor = rand() % 0xffffff | (0xff0000);

		m_data = new RSData(2);

		m_data->push(&rParams);
		m_data->push(this);

	}

	tresult PLUGIN_API ReaShaderProcessor::notify(IMessage* message)
	{
		if (!message)
			return kInvalidArgument;

		if (strcmp(message->getMessageID(), "BinaryMessage") == 0)
		{
			const void* data;
			uint32 size;
			if (message->getAttributes()->getBinary("MyData", data, size) == kResultOk)
			{
				// we are in UI thread
				// size should be 100
				if (size == 100 && ((char*)data)[1] == 1) // yeah...
				{
					fprintf(stderr, "[ReaShaderProcessor] received the binary message!\n");
				}
				return kResultOk;
			}
		}

		return AudioEffect::notify(message);
	}

	tresult ReaShaderProcessor::receiveText(const char* text)
	{
		// received from Controller
		fprintf(stderr, "[AGain] received: ");
		fprintf(stderr, "%s", text);
		fprintf(stderr, "\n");

		return kResultOk;
	}


	//------------------------------------------------------------------------
	ReaShaderProcessor::~ReaShaderProcessor()
	{

	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::initialize(FUnknown* context)
	{
		// Here the Plug-in will be instanciated

		//---always initialize the parent-------
		tresult result = AudioEffect::initialize(context);
		// if everything Ok, continue
		if (result != kResultOk)
		{
			return result;
		}

		//--- create Audio IO ------
		addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
		addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

		/* If you don't need an event bus, you can remove the next line */
		addEventInput(STR16("Event In"), 1);

		this->context = context;

		// init vulkan
		try {
			initVulkan();
		}
		catch (const std::exception& e) {
			exceptionOnInitialize = true;
			std::cerr << (std::string("Exception: ") + e.what()).c_str() << "ReaShader crashed..." << std::endl;
			return kResultFalse;
		}

		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::terminate()
	{
		// Here the Plug-in will be de-instanciated, last possibility to remove some memory!

		// clean up vulkan
		try {
			if (!exceptionOnInitialize)
				cleanupVulkan();
		}
		catch (const std::exception& e) {
			std::cerr << (std::string("Exception: ") + e.what()).c_str() << "ReaShader crashed..." << std::endl;
			return kResultFalse;
		}

		//---do not forget to call parent ------
		return AudioEffect::terminate();
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::setActive(TBool state)
	{
		//--- called when the Plug-in is enable/disable (On/Off) -----

		if (state) {

			// REAPER API (here we are after effOpen equivalent in vst3) 

			// query reaper interface
			IReaperHostApplication* reaperApp{ nullptr };
			if (context->queryInterface(IReaperHostApplication_iid, (void**)&reaperApp) == kResultOk) {

				// get create video processor
				IREAPERVideoProcessor* (*video_CreateVideoProcessor)(void* fxctx, int version) { nullptr };
				//vst2.4: *(void**)&video_CreateVideoProcessor = (void*)audioMaster(&cEffect, 0xdeadbeef, 0xdeadf00d, 0, "video_CreateVideoProcessor", 0.0f);
				*(void**)&video_CreateVideoProcessor = (void*)reaperApp->getReaperApi("video_CreateVideoProcessor");

				//vst2.4: void* ctx = (void*)audioMaster(&cEffect, 0xdeadbeef, 0xdeadf00e, 4, NULL, 0.0f);
				void* ctx = reaperApp->getReaperParent(4);

				if (ctx)
				{
					if (video_CreateVideoProcessor)
					{
						m_videoproc = video_CreateVideoProcessor(ctx, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION);
						if (m_videoproc)
						{
							m_videoproc->userdata = m_data;
							m_videoproc->process_frame = processVideoFrame;
							m_videoproc->get_parameter_value = getVideoParam;
						}
					}
				}
			}
		}
		else {
			if (m_videoproc)
				delete m_videoproc; // MUST !! (otherwise continue running processing)
			// also must be here otherwise crash
		}

		return AudioEffect::setActive(state);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::process(Vst::ProcessData& data)
	{
		//--- First : Read inputs parameter changes-----------

		if (data.inputParameterChanges)
		{
			// for each parameter defined by its ID
			int32 numParamsChanged = data.inputParameterChanges->getParameterCount();
			for (int32 index = 0; index < numParamsChanged; index++)
			{
				// for this parameter we could iterate the list of value changes (could 1 per audio block or more!)
				// in this example we get only the last value (getPointCount - 1)
				if (auto* paramQueue = data.inputParameterChanges->getParameterData(index))
				{
					Vst::ParamValue value;
					int32 sampleOffset;
					int32 numPoints = paramQueue->getPointCount();
					switch (paramQueue->getParameterId())
					{
					case ReaShaderParamIds::uAudioGain:
						if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
							rParams.at(uAudioGain) = value;
						break;
					case ReaShaderParamIds::uVideoParam:
						if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
							rParams.at(uVideoParam) = value;
						break;
					}
				}
			}
		}

		//-- Flush case: we only need to update parameter, noprocessing possible
		if (data.numInputs == 0 || data.numSamples == 0)
			return kResultOk;


		//--- Here you have to implement your processing

		int32 numChannels = data.inputs[0].numChannels;

		//---get audio buffers using helper-functions(vstaudioprocessoralgo.h)-------------
		uint32 sampleFramesSize = getSampleFramesSizeInBytes(processSetup, data.numSamples);
		void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
		void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

		// Here could check the silent flags
		// now we will produce the output
		// mark our outputs has not silent
		data.outputs[0].silenceFlags = 0;

		// optionally derive another value from param default range [0,1]
		float gain = rParams.at(uAudioGain);
		// for each channel (left and right)
		for (int32 i = 0; i < numChannels; i++)
		{
			int32 samples = data.numSamples;
			Vst::Sample32* ptrIn = (Vst::Sample32*)in[i];
			Vst::Sample32* ptrOut = (Vst::Sample32*)out[i];
			Vst::Sample32 tmp;
			// for each sample in this channel
			while (--samples >= 0)
			{
				// apply gain
				tmp = (*ptrIn++) * gain;
				(*ptrOut++) = tmp;
			}
		}

		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::setupProcessing(Vst::ProcessSetup& newSetup)
	{
		//--- called before any processing ----
		return AudioEffect::setupProcessing(newSetup);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::canProcessSampleSize(int32 symbolicSampleSize)
	{
		// by default kSample32 is supported
		if (symbolicSampleSize == Vst::kSample32)
			return kResultTrue;

		// disable the following comment if your processing support kSample64
		/* if (symbolicSampleSize == Vst::kSample64)
			return kResultTrue; */

		return kResultFalse;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::setState(IBStream* state)
	{
		if (!state)
			return kResultFalse;

		// called when we load a preset or project, the model has to be reloaded
		IBStreamer streamer(state, kLittleEndian);

		// make sure everything is read in order (in the exact way it was saved)

		for (auto const& [uParamId, value] : rParams) {
			float savedParam = 0.f;
			if (streamer.readFloat(savedParam) == false)
				return kResultFalse;
			rParams.at(uParamId) = savedParam;
		}

		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderProcessor::getState(IBStream* state)
	{
		// here we need to save the model
		IBStreamer streamer(state, kLittleEndian);

		// make sure everything is saved in order
		for (auto const& [uParamId, value] : rParams) {
			streamer.writeFloat(value);
		}

		this->sendTextMessage("hellooooo");

		return kResultOk;
	}

	//------------------------------------------------------------------------

} // namespace ReaShader
