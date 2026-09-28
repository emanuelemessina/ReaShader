//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/vst/ivstmessage.h"
#include <public.sdk/source/vst/vstaudioeffect.h>
#include <public.sdk/source/vst/vstcomponent.h>

#include "rsprocessor.h"
#include <pluginterfaces/vst/ivstaudioprocessor.h>

using namespace Steinberg;
using namespace Vst;

namespace ReaShader
{
	//------------------------------------------------------------------------
	//  MyPluginProcessor
	//------------------------------------------------------------------------
	class MyPluginProcessor : public Steinberg::Vst::AudioEffect
	{
	  public:
		MyPluginProcessor();

		// Create function
		static Steinberg::FUnknown* createInstance(void* /*context*/)
		{
			return (Steinberg::Vst::IAudioProcessor*)new MyPluginProcessor;
		}

		//------------------------------------------------------------------------
		// AudioEffect overrides:
		//------------------------------------------------------------------------
		/** Called at first after constructor */
		Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;

		/** Called at the end before destructor */
		Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

		/** Switch the Plug-in on/off */
		Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;

		/** Will be called before any process call */
		Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;

		/** Asks if a given sample size is supported see SymbolicSampleSizes. */
		Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

		/** Here we go...the process call */
		Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

		/** For persistence */
		Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

		//------------------------------------------------------------------------

		/** We want to receive message. */
		Steinberg::tresult PLUGIN_API notify(Vst::IMessage* message) SMTG_OVERRIDE;
		/** Test of a communication channel between controller and component */
		tresult receiveText(const char* text) SMTG_OVERRIDE;

		//------------------------------------------------------------------------

	  protected:
		std::unique_ptr<ReaShaderProcessor> reaShaderProcessor;
		friend class ReaShaderProcessor;
	};
} // namespace ReaShader
