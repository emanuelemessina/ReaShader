//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#include "myplugincontroller.h"

#include <stdio.h>

#include "rscontroller.h"
#include "rsui/vst/rsuieditor.h"

using namespace Steinberg;
using namespace VSTGUI;

namespace ReaShader
{
	//------------------------------------------------------------------------
	// MyPluginController Implementation
	//------------------------------------------------------------------------

	tresult PLUGIN_API MyPluginController::initialize(FUnknown* context)
	{
		// Here the Plug-in will be instanciated

		//---do not forget to call parent ------
		tresult result = EditControllerEx1::initialize(context);
		if (result != kResultOk)
		{
			return result;
		}

		reaShaderController = std::make_unique<ReaShaderController>(context, this);

		reaShaderController->initialize();

		return kResultOk;
	}

	/**
	 * @brief We will use plugin controller - processor text communication as a tunnel to frontend webui - rsprocessor
	 * communication. Received message from processor, relay back to webui.
	 */
	tresult MyPluginController::receiveText(const char* text)
	{
		if (text)
		{
			// recieved message from processor -> need to update the web ui
			// relay ws message to frontend
			std::string msg(text);
			reaShaderController->rsuiServer->sendWSTextMessage(msg);
		}

		return kResultOk;
	}

	tresult PLUGIN_API MyPluginController::notify(IMessage* message)
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
					fprintf(stderr, "[MyPluginController] received the binary message!\n");
				}
				return kResultOk;
			}
		}

		return EditControllerEx1::notify(message);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginController::terminate()
	{
		// Here the Plug-in will be de-instanciated, last possibility to remove some memory!

		reaShaderController->terminate();

		//---do not forget to call parent ------
		return EditControllerEx1::terminate();
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginController::setComponentState(IBStream* state)
	{
		// Here you get the state of the component (Processor part)
		if (!state)
			return kResultFalse;

		reaShaderController->loadParamsUIValues(state);

		return kResultOk;
	}

	//------------------------------------------------------------------------
	IPlugView* PLUGIN_API MyPluginController::createView(FIDString name)
	{
		// Here the Host wants to open your editor (if you have one)
		if (FIDStringsEqual(name, Vst::ViewType::kEditor))
		{
			// create your editor here and return a IPlugView ptr of it
			return reaShaderController->createVSTView();
		}
		return nullptr;
	}

	IController* MyPluginController::createSubController(UTF8StringPtr name, const IUIDescription* description,
														 VST3Editor* editor)
	{
		if (UTF8StringView(name) == "RSUI")
		{
			return reaShaderController->createVSTViewSubController(description);
		}
		return nullptr;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginController::setParamNormalized(Vst::ParamID tag, Vst::ParamValue value)
	{
		// called by host to update your parameters
		tresult result = EditControllerEx1::setParamNormalized(tag, value);
		return result;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginController::getParamStringByValue(Vst::ParamID tag, Vst::ParamValue valueNormalized,
																 Vst::String128 string)
	{
		// called by host to get a string for given normalized value of a specific parameter
		// (without having to set the value!)
		return EditControllerEx1::getParamStringByValue(tag, valueNormalized, string);
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginController::getParamValueByString(Vst::ParamID tag, Vst::TChar* string,
																 Vst::ParamValue& valueNormalized)
	{
		// called by host to get a normalized value from a string representation of a specific parameter
		// (without having to set the value!)
		return EditControllerEx1::getParamValueByString(tag, string, valueNormalized);
	}

	//------------------------------------------------------------------------

	tresult PLUGIN_API MyPluginController::setState(IBStream* state)
	{
		// Here you get the state of the controller

		return kResultTrue;
	}

	//------------------------------------------------------------------------
	tresult PLUGIN_API MyPluginController::getState(IBStream* state)
	{
		// Here you are asked to deliver the state of the controller (if needed)
		// Note: the real state of your plug-in is saved in the processor

		return kResultTrue;
	}
	//------------------------------------------------------------------------
} // namespace ReaShader