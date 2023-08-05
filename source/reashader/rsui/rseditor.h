#pragma once

#include "tools/fwd_decl.h"

#include "vstgui/lib/iviewlistener.h"
#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/uidescription/icontroller.h"
#include "vstgui/uidescription/uiattributes.h"

namespace ReaShader
{
	FWD_DECL(ReaShaderController)
	FWD_DECL(MyPluginController)

	using namespace VSTGUI;

	enum RSUITags
	{
		tButtonRSUIOpen
	};

	class RSUIController : public VSTGUI::IController, public VSTGUI::ViewListenerAdapter
	{
	  public:
		RSUIController(ReaShaderController* controller, VST3Editor *editor,
					   const IUIDescription *description);

		//--- from IControlListener ----------------------

		void valueChanged(CControl *pControl) override;

		void controlBeginEdit(CControl *pControl) override;

		void controlEndEdit(CControl *pControl) override;

		// ---- from IController -----------

		//--- is called when a view will be deleted: the editor is closed -----
		void viewWillDelete(CView *view) override;

		//--- from IViewListenerAdapter ----------------------

		//--- is called when the view is loosing the focus -----------------
		void viewLostFocus(CView *view) override;

		//--- is called when a view is created -----
		CView *verifyView(CView *view, const UIAttributes &attributes, const IUIDescription *description);

		//-------------------------------------------

	  protected:
		// upper references
		ReaShaderController* reaShaderController;

		// item of subcontroller but not in main subcontainer

		// main container of subcontroller
		CViewContainer *rsuiContainer{nullptr};

		// children of main container

	  private:
		const std::string l_rsui_container = "rsui";
		const std::string l_rs_version = "rs-version";
		const std::string l_button_rsui_open = "button-rsui-open";

		std::string _ui_url;
	};

	class RSEditor : public VST3Editor, public ViewMouseListenerAdapter
	{
	  public:
		RSEditor(MyPluginController* controller, UTF8StringPtr templateName, UTF8StringPtr xmlFile);
		
	  protected:
		// ------ from CommandMenuItemTargetAdapter --------

		bool validateCommandMenuItem(CCommandMenuItem *item) override;
		bool onCommandMenuItemSelected(CCommandMenuItem *item) override;
		//--------------------------

		void onMouseEvent(MouseEvent &event, CFrame *frame) override;

	  private:
	};
} // namespace ReaShader
