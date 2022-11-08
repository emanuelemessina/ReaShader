//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#include "myplugincontroller.h"
#include "myplugincids.h"
#include "base/source/fstreamer.h"

#include <stdio.h>
#include <chrono>

#include "tools/paths.h"

#include "rsui/rseditor.h"

#include <restinio/all.hpp> // must be included in this translation unit otherwise conflict with windows.h

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

		// Here you could register some parameters

		parameters.addParameter(STR16("Audio Gain"), STR16("dB"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uAudioGain);
		parameters.addParameter(STR16("Video Param"), STR16("units"), 0, .5, Vst::ParameterInfo::kCanAutomate, ReaShaderParamIds::uVideoParam);

		// Launch UI Server Thread

		restinio::running_server_instance_t<restinio::http_server_t<restinio::default_traits_t>>* uiserver_handle = restinio::run_async(
			restinio::own_io_context(),
			restinio::server_settings_t<restinio::default_traits_t>{}
		.port(8080)
			.address("localhost")
			.request_handler([](auto req) {
			return req->create_response().set_body("Hello, World!").done();
				})
			.cleanup_func([&] {
					// called when server is shutting down
				}),
					1
					).release();

				_uiserver = uiserver_handle;

				return result;
	}

	tresult ReaShaderController::receiveText(const char* text)
	{
		// received from Component
		if (text)
		{
			fprintf(stderr, "[AGainController] received: ");
			fprintf(stderr, "%s", text);
			fprintf(stderr, "\n");
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
		std::any_cast<restinio::running_server_instance_t<restinio::http_server_t<restinio::default_traits_t>>*>(_uiserver)->stop();

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
	IPlugView* PLUGIN_API ReaShaderController::createView(FIDString name)
	{
		// Here the Host wants to open your editor (if you have one)
		if (FIDStringsEqual(name, Vst::ViewType::kEditor))
		{
			// create your editor here and return a IPlugView ptr of it
			editor = new RSEditor(this, "view", "myplugineditor.uidesc");

			auto description = editor->getUIDescription();

			std::string viewName = "myview";
			auto* debugAttr = new VSTGUI::UIAttributes();
			debugAttr->setAttribute(VSTGUI::UIViewCreator::kAttrClass, "CViewContainer");
			debugAttr->setAttribute("size", "300, 300");

			description->addNewTemplate(viewName.c_str(), debugAttr);

			//view->exchangeView("myview");
			return editor;
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
			rseditor->subController = controller;
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
} // namespace ReaShader
