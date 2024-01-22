/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "rscontroller.h"
#include "myplugincids.h"
#include "myplugincontroller.h"
#include "rsparams/rsparams.h"
#include "rsui/api.h"
#include "rsui/vst/rsuieditor.h"
#include <base/source/fstreamer.h>

#include "tools/logging.h"

namespace ReaShader
{
	using namespace RSUI;

	void ReaShaderController::_unregisterVSTParams()
	{
		vstParameters->removeAll();
	}
	void ReaShaderController::_registerVSTParam(Parameters::VSTParameter& rsParam)
	{
		vstParameters->addParameter(
			(Steinberg::char16*)tools::strings::string_to_u16string(rsParam.title).c_str(),
									(Steinberg::char16*)tools::strings::string_to_u16string(rsParam.units).c_str(), 0,
									rsParam.defaultValue, rsParam.steinbergFlags, rsParam.id);
	}
	void ReaShaderController::_registerVSTParams()
	{
		// here we only register the vst automation params, we discard everything else in the complete state
		for (int i = 0; i < controller_rsParams.size(); i++)
		{
			if (controller_rsParams[i]->typeId() == Parameters::Type::VSTParameter)
				_registerVSTParam(dynamic_cast<Parameters::VSTParameter&>(*controller_rsParams[i]));
		}
	}

	void ReaShaderController::_receiveTextFromRSUIServer(const std::string& msg)
	{
		// relay messages to processor if needed

		// check json
		RSUI::MessageHandler(msg.c_str())
			.reactToVSTParamUpdate([&](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue newValue) {
				dynamic_cast<Parameters::VSTParameter&>(*controller_rsParams[id]).value = newValue;
				myPluginController->setParamNormalized(id, newValue); // update vst param value
				_relayMessageToProcessor(msg); // relay to processor to update processor params
			}) 
			.reactToParamUpdate([&](Steinberg::Vst::ParamID id, json newValue) { 
				controller_rsParams[id]->setValue(newValue);
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
					case RSUI::RequestType::WantParamTypesList: {
						std::string resp = RSUI::MessageBuilder::buildParamTypesList().dump();
						rsuiServer->sendWSTextMessage(resp);
						break;
					}
					default: // relay to processor to handle unknown request
						_relayMessageToProcessor(msg);
						break;
				}
			})
			.reactToRenderingDeviceChange([&](int newIndex) {
				// update rendering device controller parameter
				dynamic_cast<Parameters::Int8u&>(*controller_rsParams[Parameters::uRenderingDevice]).value = (Steinberg::Vst::ParamValue)newIndex;
				_relayMessageToProcessor(msg); // relay to processor to actually perform the change
			})
			.reactToParamAdd([&](std::unique_ptr<Parameters::IParameter> newParam) {
				newParam->id = controller_rsParams.size();
				controller_rsParams.push_back(std::move(newParam));
				
				if (newParam->typeId() == Parameters::Type::VSTParameter)
					_registerVSTParam(dynamic_cast<Parameters::VSTParameter&>(*newParam));
				
				_relayMessageToProcessor(msg); // relay to processor to update its param list
			})
			.fallback([&](const json& parsedMsg) {
				_relayMessageToProcessor(msg); // relay to processor in the default react case
			});
	}

	void ReaShaderController::_relayMessageToProcessor(const std::string& msg)
	{
		myPluginController->sendTextMessage(msg.c_str()); 
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
		// get parameter container
		vstParameters = &(myPluginController->getParameters());
		
		// (already done in the load state function)
		/*
		// populate default params
		controller_rsParams.assign(defaultRSParams, std::end(defaultRSParams));

		// Here you register parameters for the vst ui (used for automation)
		_registerUIParams();
		*/

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

	// called after the processor wrote or loaded a state
	void ReaShaderController::loadUIParams(IBStream* state)
	{
		// they are taken from the processor saved state
		// so no need to prepopulate and register params from defaults

		// populate the params vector
		Parameters::PresetStreamer streamer{ state };
		streamer.read(controller_rsParams, [&](std::string&& msg) {
			LOG(WARNING, toConsole | toFile | toBox, "ReaShaderProcessor", "RSPresetStreamer read error", std::move(msg));
		});

		// erase the param container
		_unregisterVSTParams();
		// re register
		_registerVSTParams();

		// just update a param
		//myPluginController->setParamNormalized(uParamId, savedParam);
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
	// intercepts a message that the processor sent to the webui
	void ReaShaderController::interceptWebuiMessageFromProcessor(std::string& msg)
	{
		RSUI::MessageHandler(msg.c_str()).reactToVSTParamUpdate(
			[&](Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue newValue) {
				// just update the internal controller param
				dynamic_cast<Parameters::VSTParameter&>(*controller_rsParams[id]).value = newValue;
			});
	}
} // namespace ReaShader