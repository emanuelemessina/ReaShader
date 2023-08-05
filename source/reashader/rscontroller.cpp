#include "rscontroller.h"
#include "myplugincids.h"
#include "myplugincontroller.h"
#include "rsparams.h"
#include "rsui/rseditor.h"

#include <base/source/fstreamer.h>

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

	void ReaShaderController::initialize()
	{
		// Here you register parameters for the vst ui (used for automation)
		_registerUIParams();

		// Launch UI Server Thread
		rsuiServer = std::make_unique<RSUIServer>(myPluginController);
	}

	void ReaShaderController::sendMessageToUI(const char* text)
	{
		// recieved message from processor -> need to update the web ui
		// relay ws message to frontend
		rsuiServer->sendWSMessage(text);
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
			{ // TODO: ERROR
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
	RSUIController* ReaShaderController::createViewSubController(const IUIDescription* description)
	{
		rsuiController = new RSUIController(this, rsEditor, description);
		return rsuiController;
	}
} // namespace ReaShader