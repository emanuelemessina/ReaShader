//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#pragma once


#include "pluginterfaces/vst/ivstmessage.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

#include "vstgui/lib/iviewlistener.h"
#include "vstgui/uidescription/icontroller.h"

#include "vstgui/uidescription/delegationcontroller.h"

#include <thread>
#include <mutex>
#include <any>

using namespace Steinberg;
using namespace Vst;
using namespace VSTGUI;

namespace ReaShader {


	//------------------------------------------------------------------------
	//  ReaShaderController
	//------------------------------------------------------------------------
	class ReaShaderController : public Steinberg::Vst::EditControllerEx1, public VSTGUI::VST3EditorDelegate
	{
	public:
		//------------------------------------------------------------------------
		ReaShaderController() = default;
		~ReaShaderController() SMTG_OVERRIDE = default;

		// Create function
		static Steinberg::FUnknown* createInstance(void* /*context*/)
		{
			return (Steinberg::Vst::IEditController*)new ReaShaderController;
		}

		// IPluginBase
		Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

		// EditController
		Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
		Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag,
			Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID tag,
			Steinberg::Vst::ParamValue valueNormalized,
			Steinberg::Vst::String128 string) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID tag,
			Steinberg::Vst::TChar* string,
			Steinberg::Vst::ParamValue& valueNormalized) SMTG_OVERRIDE;

		auto getParameters() {
			return &parameters;
		}

		//---from ComponentBase-----
		tresult receiveText(const char* text) SMTG_OVERRIDE;
		tresult PLUGIN_API notify(IMessage* message) SMTG_OVERRIDE;

		//---from VST3EditorDelegate-----------
		VSTGUI::IController* createSubController(VSTGUI::UTF8StringPtr name, const VSTGUI::IUIDescription* description,
			VSTGUI::VST3Editor* editor) SMTG_OVERRIDE;

		//---Interface---------
		DEFINE_INTERFACES
			// Here you can add more supported VST3 interfaces
			// DEF_INTERFACE (Vst::IXXX)
			END_DEFINE_INTERFACES(EditController)
			DELEGATE_REFCOUNT(EditController)

		//------------------------------------------------------------------------

		// GETTERS

		uint16_t getUiServerPort() { return _uiserver_port; }

	//------------------------------------------------------------------------

	protected:
		VST3Editor* _editor{ nullptr };

		std::any _uiserver{ nullptr };
		uint16_t _uiserver_port{ 0 };
	};

	//------------------------------------------------------------------------
} // namespace ReaShader
