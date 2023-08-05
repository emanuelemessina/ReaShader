#pragma once

#include <memory>

#include "rsui/backend.h"
#include "tools/fwd_decl.h"

#include "rsui/rseditor.h"

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
		RSUIController* createViewSubController(const IUIDescription* description);

		void terminate();

		std::unique_ptr<RSUIServer> rsuiServer;

	  private:
		MyPluginController* myPluginController;
		FUnknown* context;

		void _registerUIParams();

		// memory managed by plugin
		RSEditor* rsEditor;
		RSUIController* rsuiController;
	};
} // namespace ReaShader