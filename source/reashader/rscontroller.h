#pragma once

#include <memory>

#include "rsui/backend/backend.h"
#include "tools/fwd_decl.h"

#include "rsui/vst/rsuieditor.h"

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

		void loadParamsUIValues(IBStream* state);
		IPlugView* createVSTView();
		RSUISubController* createVSTViewSubController(const IUIDescription* description);

		void terminate();

		std::unique_ptr<RSUIServer> rsuiServer;

	  private:
		MyPluginController* myPluginController;
		FUnknown* context;

		void _registerUIParams();

		void _receiveTextFromRSUIServer(const std::string& msg);
		void _receiveBinaryFromRSUIServer(const std::vector<char>& msg);

		// memory managed by plugin
		RSUIEditor* rsEditor;
		RSUISubController* rsuiController;
	};
} // namespace ReaShader