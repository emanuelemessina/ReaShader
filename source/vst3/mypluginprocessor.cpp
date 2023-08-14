//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#include "mypluginprocessor.h"
#include "myplugincids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include <nlohmann/json.hpp>

#include "rsparams.h"
#include "tools/exceptions.h"

using namespace Steinberg;
using json = nlohmann::json;

namespace ReaShader
{
	//------------------------------------------------------------------------
	// MyPluginProcessor
	//------------------------------------------------------------------------
	MyPluginProcessor::MyPluginProcessor()
	{
		//--- set the wanted controller for our processor
		setControllerClass(kReaShaderControllerUID);
	}

	tresult PLUGIN_API MyPluginProcessor::notify(IMessage* message)
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
					fprintf(stderr, "[MyPluginProcessor] received the binary message!\n");
				}
				return kResultOk;
			}
		}

		return AudioEffect::notify(message);
	}

	tresult MyPluginProcessor::receiveText(const char* text)
	{
		// checkmessage from rsuiserver
		try
		{
			auto msg = json::parse(text);
			reaShaderProcessor->receivedJSONFromController(msg);
		}
		catch (STDEXC /*e*/)
		{
			// the message is not a webui message
			// json parse exception is catched and logged by controller
		}

		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginProcessor::initialize(FUnknown* context)
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

		reaShaderProcessor = std::make_unique<ReaShaderProcessor>(this, context);
		reaShaderProcessor->initialize();

		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginProcessor::terminate()
	{
		// Here the Plug-in will be de-instanciated, last possibility to remove some memory!

		reaShaderProcessor->terminate();

		//---do not forget to call parent ------
		return AudioEffect::terminate();
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginProcessor::setActive(TBool state)
	{
		//--- called when the Plug-in is enable/disable (On/Off) -----

		if (state)
		{
			reaShaderProcessor->activate();
		}
		else
		{
			reaShaderProcessor->deactivate();
		}

		return AudioEffect::setActive(state);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginProcessor::process(Vst::ProcessData& data)
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
					/*switch (paramQueue->getParameterId())
					{
					case ReaShaderParamIds::uAudioGain:
						if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
							rParams.at(uAudioGain) = value;
						break;
					case ReaShaderParamIds::uVideoParam:
						if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
							rParams.at(uVideoParam) = value;
						break;
					}*/
					if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
					{
						reaShaderProcessor->parameterUpdate(paramQueue->getParameterId(), value);
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

		// uint32 sampleFramesSize = getSampleFramesSizeInBytes(processSetup, data.numSamples);
		void** in = getChannelBuffersPointer(processSetup, data.inputs[0]);
		void** out = getChannelBuffersPointer(processSetup, data.outputs[0]);

		// Here could check the silent flags
		// now we will produce the output
		// mark our outputs has not silent
		data.outputs[0].silenceFlags = 0;

		// optionally derive another value from param default range [0,1]
		float gain = reaShaderProcessor->processor_rsParams[uAudioGain].value;
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
	tresult PLUGIN_API MyPluginProcessor::setupProcessing(Vst::ProcessSetup& newSetup)
	{
		//--- called before any processing ----
		return AudioEffect::setupProcessing(newSetup);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginProcessor::canProcessSampleSize(int32 symbolicSampleSize)
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
	tresult PLUGIN_API MyPluginProcessor::setState(IBStream* state)
	{
		if (!state)
			return kResultFalse;

		reaShaderProcessor->loadParamsValues(state);

		return kResultOk;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginProcessor::getState(IBStream* state)
	{
		reaShaderProcessor->storeParamsValues(state);

		return kResultOk;
	}

	//------------------------------------------------------------------------
} // namespace ReaShader