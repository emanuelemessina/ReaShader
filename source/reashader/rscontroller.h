/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <memory>

#include "rsui/backend/backend.h"
#include "tools/fwd_decl.h"

#include "rsui/vst/rsuieditor.h"
#include "rsparams/rsparams.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace Steinberg;
using namespace Vst;

namespace ReaShader
{
	FWD_DECL(MyPluginController)
	FWD_DECL(RSUI::RSUIEditor)

	using namespace RSUI;

	class ReaShaderController
	{
	  public:
		ReaShaderController(FUnknown* context, MyPluginController* myPluginController)
			: context(context), myPluginController(myPluginController)
		{
		}

		void initialize();

		void loadUIParams(IBStream* state);
		IPlugView* createVSTView();
		RSUISubController* createVSTViewSubController(const IUIDescription* description);

		void interceptWebuiMessageFromProcessor(std::string& msg);

		void terminate();

		std::unique_ptr<RSUIServer> rsuiServer;

		std::vector<std::unique_ptr<Parameters::IParameter>> controller_rsParams;

	  private:
		MyPluginController* myPluginController;
		FUnknown* context;
		ParameterContainer* vstParameters{ nullptr };

		void _unregisterVSTParams();
		void _registerVSTParam(Parameters::VSTParameter& rsParam);
		void _registerVSTParams();

		void _receiveTextFromRSUIServer(const std::string&& msg);
		void _receiveBinaryFromRSUIServer(const std::vector<char>&& msg);
		void _receiveFileFromRSUIServer(json&& metadata, std::string&& name, std::string&& extension, size_t size,
										const std::vector<char>&& data);

		void _relayMessageToProcessor(const std::string& msg);

		// memory managed by plugin
		RSUIEditor* rsEditor{nullptr};
		RSUISubController* rsuiController{nullptr};
	};
} // namespace ReaShader