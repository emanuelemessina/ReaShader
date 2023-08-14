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
			parameters.addParameter(controller_rsParams[i].title.c_str(), controller_rsParams[i].units.c_str(), 0,
									controller_rsParams[i].defaultValue, controller_rsParams[i].steinbergFlags,
									controller_rsParams[i].id);
		}
	}

	void ReaShaderController::_receiveTextFromRSUIServer(const std::string& msg)
	{
		// relay messages to processor if needed

		// check json
		RSUI::MessageHandler(msg.c_str())
			.reactToParamUpdate([&](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue newValue) {
				controller_rsParams[id].value = newValue;
				myPluginController->setParamNormalized(id, newValue);
				myPluginController->sendTextMessage(msg.c_str()); // relay to processor to update processor params
			})
			.reactToRequest([&](RSUI::RequestType type) {
				switch (type)
				{
					case RSUI::RequestType::WantParamsList: {
						std::string resp = RSUI::MessageBuilder::buildParamsList(controller_rsParams).dump();
						rsuiServer->sendWSTextMessage(resp);
						break;
					}
					case RSUI::RequestType::WantParamGroupsList: {
						std::string resp = RSUI::MessageBuilder::buildParamGroupsList().dump();
						rsuiServer->sendWSTextMessage(resp);
						break;
					}
					default: // relay to processor to handle unknown request
						myPluginController->sendTextMessage(msg.c_str());
						break;
				}
			})
			.reactToRenderingDeviceChange([&](int newIndex) {
				// update rendering device controller parameter
				controller_rsParams[uRenderingDevice].value = (Steinberg::Vst::ParamValue)newIndex;
				myPluginController->sendTextMessage(msg.c_str()); // relay to processor to actually perform the change
			})
			.fallback([&](const json& parsedMsg) {
				myPluginController->sendTextMessage(msg.c_str()); // relay to processor in the default react case
			});
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
		// populate default params
		controller_rsParams.assign(defaultRSParams, std::end(defaultRSParams));

		// Here you register parameters for the vst ui (used for automation)
		_registerUIParams();

		// Launch UI Server Thread
		rsuiServer = std::make_unique<RSUIServer>(
			std::bind(&ReaShaderController::_receiveTextFromRSUIServer, this, std::placeholders::_1),
			std::bind(&ReaShaderController::_receiveBinaryFromRSUIServer, this, std::placeholders::_1));
	}

	void ReaShaderController::terminate()
	{
		// send death message to frontend
		std::string death = RSUI::MessageBuilder::buildServerShutdown().dump();
		rsuiServer->sendWSTextMessage(death);
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
				std::wstring title(controller_rsParams[uParamId].title);
				LOG(WARNING, toConsole | toFile | toBox, "ReaShaderController", "IBStreamer read error",
					std::format("Cannot read param {} with id {}", tools::strings::ws2s(title), uParamId));
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