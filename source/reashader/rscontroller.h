#pragma once

#include <memory>

#include "rsui/backend.h"
#include "tools/fwd_decl.h"

#include "rsui/rseditor.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace Steinberg;
using namespace Vst;

namespace ReaShader
{
	FWD_DECL(MyPluginController)
	FWD_DECL(RSEditor)

	class ReaShaderController
	{
	  public:
		ReaShaderController(FUnknown* context, MyPluginController* myPluginController)
			: context(context), myPluginController(myPluginController)
		{
		}

		void initialize();

		void sendMessageToUI(const char* text);
		void loadParamsUIValues(IBStream* state);
		IPlugView* createVSTView();
		RSUIController* createVSTViewSubController(const IUIDescription* description);

		void terminate();

		std::unique_ptr<RSUIServer> rsuiServer;

	  private:
		MyPluginController* myPluginController;
		FUnknown* context;

		void _registerUIParams();

		void _receiveTextFromWebUI(const std::string& msg);
		void _receiveBinaryFromWebUI(const std::vector<char>& msg);

		// memory managed by plugin
		RSEditor* rsEditor;
		RSUIController* rsuiController;
	};
} // namespace ReaShader