#include "rscontroller.h"
#include "myplugincids.h"
#include "myplugincontroller.h"
#include "rsparams.h"
#include "rsui/rseditor.h"

#include <base/source/fstreamer.h>

#include "tools/logging.h"

namespace ReaShader
{
	void ReaShaderController::_registerUIParams()
	{
		ParameterContainer& parameters = myPluginController->getParameters();

		for (int i = 0; i < uNumParams; i++)
		{
			parameters.addParameter(rsParams[i].title, rsParams[i].units, 0, rsParams[i].defaultValue,
									rsParams[i].flags, rsParams[i].id);
		}
	}

	void ReaShaderController::_receiveTextFromWebUI(const std::string& msg)
	{
		// relay message to processor
		myPluginController->sendTextMessage(msg.c_str());

		// check json
		try
		{
			auto packet = json::parse(msg);

			std::string type = packet["type"];

			if (type == "paramUpdate")
			{
				myPluginController->setParamNormalized((Steinberg::Vst::ParamID)packet["paramId"],
													   (Steinberg::Vst::ParamValue)packet["value"]);
			}
		}
		catch (STDEXC e)
		{
			LOG(WARNING, toConsole | toFile, "ReaShaderController", "Cannot parse message from web UI",
				std::format("Received: {} \n Error: {}", msg, e.what()));
		}
	}

	void ReaShaderController::_receiveBinaryFromWebUI(const std::vector<char>& msg)
	{
		// relay message to processor
		// myPluginController->sendMessage(msg);

		if (msg.size() < 2)
			// malformed: too short
			return;

		char magicSequence[3];
		magicSequence[0] = msg[0];
		magicSequence[1] = msg[1];
		magicSequence[2] = msg[2];

		if (magicSequence[2] != '\0')
			// malformed: wrong magic terminator
			return;

		std::string magic = "RS";

		if (std::string(magicSequence) != magic)
			// malformed: wrong magic
			return;

		// proceed
	}

	void ReaShaderController::initialize()
	{
		// Here you register parameters for the vst ui (used for automation)
		_registerUIParams();

		// Launch UI Server Thread
		rsuiServer = std::make_unique<RSUIServer>(
			std::bind(&ReaShaderController::_receiveTextFromWebUI, this, std::placeholders::_1),
			std::bind(&ReaShaderController::_receiveBinaryFromWebUI, this, std::placeholders::_1));
	}

	void ReaShaderController::sendMessageToUI(const char* text)
	{
		// recieved message from processor -> need to update the web ui
		// relay ws message to frontend
		std::string msg(text);
		rsuiServer->sendWSTextMessage(msg);
	}

	void ReaShaderController::terminate()
	{
	}

	void ReaShaderController::loadParamsUIValues(IBStream* state)
	{
		// basically the same code in the processor
		// update ui with processor state (setParam)

		IBStreamer streamer(state, kLittleEndian);

		for (int uParamId = 0; uParamId < uNumParams; uParamId++)
		{
			float savedParam = 0.f;
			if (streamer.readFloat((float&)savedParam) == false)
			{
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderController", "IBStreamer read error",
					std::format("Cannot read param w/ id {}", uParamId));
			}
			// in the processor: rParams.at(uParamId) = savedParam;
			myPluginController->setParamNormalized(uParamId, savedParam);
		}
	}

	IPlugView* ReaShaderController::createVSTView()
	{
		rsEditor = new RSEditor(myPluginController, "view", "myplugineditor.uidesc");
		return rsEditor;
	}
	RSUIController* ReaShaderController::createVSTViewSubController(const IUIDescription* description)
	{
		rsuiController = new RSUIController(this, rsEditor, description);
		return rsuiController;
	}
} // namespace ReaShader