#include "rscontroller.h"
#include "myplugincids.h"
#include "myplugincontroller.h"
#include "rsparams.h"
#include "rsui/api.h"
#include "rsui/vst/rsuieditor.h"
#include <base/source/fstreamer.h>

#include "tools/logging.h"

namespace ReaShader
{
	using namespace RSUI;

	void ReaShaderController::_registerUIParams()
	{
		ParameterContainer& parameters = myPluginController->getParameters();

		for (int i = 0; i < uNumParams; i++)
		{
			parameters.addParameter(rsParams[i].title, rsParams[i].units, 0, rsParams[i].defaultValue,
									rsParams[i].flags, rsParams[i].id);
		}
	}

	void ReaShaderController::_receiveTextFromRSUIServer(const std::string& msg)
	{
		// relay message to processor
		myPluginController->sendTextMessage(msg.c_str());

		// check json
		RSUI::MessageHandler(msg.c_str())
			.reacToParamUpdate([&](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue newValue) {
				myPluginController->setParamNormalized(id, newValue);
			})
			.fallbackWarning("ReaShaderController");
	}

	void ReaShaderController::_receiveBinaryFromRSUIServer(const std::vector<char>& msg)
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
			std::bind(&ReaShaderController::_receiveTextFromRSUIServer, this, std::placeholders::_1),
			std::bind(&ReaShaderController::_receiveBinaryFromRSUIServer, this, std::placeholders::_1));
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
		rsEditor = new RSUIEditor(myPluginController, "view", "myplugineditor.uidesc");
		return rsEditor;
	}
	RSUISubController* ReaShaderController::createVSTViewSubController(const IUIDescription* description)
	{
		rsuiController = new RSUISubController(this, rsEditor, description);
		return rsuiController;
	}
} // namespace ReaShader