//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#include "myplugincontroller.h"
#include "myplugincids.h"
#include "base/source/fstreamer.h"

#include <stdio.h>
#include <chrono>

#include "rsui/rseditor.h"
#include "tools/exceptions.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace Steinberg;
using namespace VSTGUI;

namespace ReaShader {
	//------------------------------------------------------------------------
	// ReaShaderController Implementation
	//------------------------------------------------------------------------

	tresult PLUGIN_API ReaShaderController::initialize(FUnknown* context)
	{
		// Here the Plug-in will be instanciated

		//---do not forget to call parent ------
		tresult result = EditControllerEx1::initialize(context);
		if (result != kResultOk)
		{
			return result;
		}

		// Here you register parameters for the vst ui (used for automation)

		parameters.addParameter(STR16("Audio Gain"), STR16("dB"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uAudioGain);
		parameters.addParameter(STR16("Video Param"), STR16("units"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uVideoParam);

		// Launch UI Server Thread

		_rsuiServer = new RSUIServer(this);
		if (_rsuiServer->hasError())
			return kResultFalse;

		return result;
	}

	// controller recieveText is a tunnel to sendWSMessage
	tresult ReaShaderController::receiveText(const char* text)
	{
		if (text)
		{
			// recieved message from processor -> need to update the web ui
			// relay ws message to frontend
			_rsuiServer->sendWSMessage(text);
		}

		return kResultOk;
	}

	tresult PLUGIN_API ReaShaderController::notify(IMessage* message)
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
					fprintf(stderr, "[ReaShaderController] received the binary message!\n");
				}
				return kResultOk;
			}
		}

		return EditControllerEx1::notify(message);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::terminate()
	{
		// Here the Plug-in will be de-instanciated, last possibility to remove some memory!

		// terminate uiserver
		delete _rsuiServer;

		//---do not forget to call parent ------
		return EditControllerEx1::terminate();
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::setComponentState(IBStream* state)
	{
		// Here you get the state of the component (Processor part)
		if (!state)
			return kResultFalse;

		// basically the same code in the processor
		// update ui with processor state (setParam)

		IBStreamer streamer(state, kLittleEndian);

		for (int uParamId = 0; uParamId < uNumParams; uParamId++) {
			float savedParam = 0.f;
			if (streamer.readFloat((float&)savedParam) == false)
				return kResultFalse;
			// in the processor: rParams.at(uParamId) = savedParam;
			setParamNormalized(uParamId, savedParam);
		}

		return kResultOk;
	}

	//------------------------------------------------------------------------
	IPlugView* PLUGIN_API ReaShaderController::createView(FIDString name)
	{
		// Here the Host wants to open your editor (if you have one)
		if (FIDStringsEqual(name, Vst::ViewType::kEditor))
		{
			// create your editor here and return a IPlugView ptr of it
			_editor = new RSEditor(this, "view", "myplugineditor.uidesc");

			return _editor;
		}
		return nullptr;
	}

	IController* ReaShaderController::createSubController(UTF8StringPtr name,
		const IUIDescription* description,
		VST3Editor* editor)
	{
		if (UTF8StringView(name) == "RSUI")
		{
			auto* controller = new RSUIController(this, editor, description);
			RSEditor* rseditor = dynamic_cast<RSEditor*>(editor);
			rseditor->setSubController(controller);
			return controller;
		}
		return nullptr;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::setParamNormalized(Vst::ParamID tag, Vst::ParamValue value)
	{
		// called by host to update your parameters
		tresult result = EditControllerEx1::setParamNormalized(tag, value);
		return result;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::getParamStringByValue(Vst::ParamID tag, Vst::ParamValue valueNormalized, Vst::String128 string)
	{
		// called by host to get a string for given normalized value of a specific parameter
		// (without having to set the value!)
		return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::getParamValueByString(Vst::ParamID tag, Vst::TChar* string, Vst::ParamValue& valueNormalized)
	{
		// called by host to get a normalized value from a string representation of a specific parameter
		// (without having to set the value!)
		return EditControllerEx1::getParamValueByString(tag, string, valueNormalized);
	}

	//------------------------------------------------------------------------

	tresult PLUGIN_API ReaShaderController::setState(IBStream* state)
	{
		// Here you get the state of the controller

		return kResultTrue;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API ReaShaderController::getState(IBStream* state)
	{
		// Here you are asked to deliver the state of the controller (if needed)
		// Note: the real state of your plug-in is saved in the processor

		return kResultTrue;
	}
	//------------------------------------------------------------------------
} // namespace ReaShader