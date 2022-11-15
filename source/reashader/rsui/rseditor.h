#pragma once

#include "vstgui/plugin-bindings/vst3editor.h"
#include "vstgui/lib/iviewlistener.h"
#include "vstgui/uidescription/icontroller.h"

#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"
#include "vstgui/uidescription/viewcreator/viewcreator.h"

#include "tools/paths.h"

#include "myplugincontroller.h"

#include "version.h"

namespace ReaShader {

	using namespace VSTGUI;

	enum RSUITags {
		tButtonRSUIOpen
	};

	class RSUIController
		: public VSTGUI::IController,
		public VSTGUI::ViewListenerAdapter
	{
	public:

		RSUIController(
			ReaShaderController* controller,
			VST3Editor* editor,
			const IUIDescription* description
		)
			: _rsController(controller), _editor(editor)
		{
			_ui_url = std::string("http://localhost:") + std::to_string(_rsController->getUiServerPort());
		}

		//-------------------------

		//--- from IControlListener ----------------------

		void valueChanged(CControl* pControl) override {
			switch (pControl->getTag()) {

			}
		}

		void controlBeginEdit(CControl* pControl) override {

		}

		void controlEndEdit(CControl* pControl) override
		{
			switch (pControl->getTag()) {
			case tButtonRSUIOpen:
				// open ui in browser
#ifdef _WIN32
				std::string cmd = "start";
#endif
#ifdef __APPLE__
				std::string cmd = "open";
#endif
#ifdef __linux_
				std::string cmd = "xdg-open";
#endif
				system(cmd.append(" ").append(_ui_url).c_str());
				break;
			}
		}

		// ---- from IController -----------

		//--- is called when a view is created -----
		CView* verifyView(CView* view, const UIAttributes& attributes, const IUIDescription* description) override
		{
			std::string uidesc_label = "";

			{
				const std::string* uidesc_label_ptr = attributes.getAttributeValue("uidesc-label");
				if (uidesc_label_ptr) {
					uidesc_label = *uidesc_label_ptr;
				}
			}

			if (CViewContainer* container = dynamic_cast<CViewContainer*> (view))
			{
				if (uidesc_label == l_rsui_container) {
					rsuiContainer = container;
					container->setTransparency(true);
				}
			}
			else if (CTextButton* button = dynamic_cast<CTextButton*> (view)) {
				// set tag on rsui-open button
				if (uidesc_label == l_button_rsui_open) {
					button->setTag(tButtonRSUIOpen);
					button->setTitle(_ui_url.c_str());
				}
				// bind this as listener to button
				button->setListener(this);
			}
			else if (CTextLabel* label = dynamic_cast<CTextLabel*> (view)) {
				// set version string label
				if (uidesc_label == l_rs_version) {
					label->setText(FULL_VERSION_STR);
				}
			}

			return view;
		}
		//---------------------------------------------

		//--- from IViewListenerAdapter ----------------------

		//--- is called when a view will be deleted: the editor is closed -----
		void viewWillDelete(CView* view) override
		{

		}
		//--- is called when the view is loosing the focus -----------------
		void viewLostFocus(CView* view) override
		{

		}
		//-------------------------------------------

	protected:
		// upper references
		ReaShaderController* _rsController{ nullptr };
		VST3Editor* _editor;

		// item of subcontroller but not in main subcontainer


		// main container of subcontroller
		CViewContainer* rsuiContainer{ nullptr };

		// children of main container

	private:

		const std::string l_rsui_container = "rsui";
		const std::string l_rs_version = "rs-version";
		const std::string l_button_rsui_open = "button-rsui-open";

		std::string _ui_url;
	};

	class RSEditor
		: public VST3Editor,
		public ViewMouseListenerAdapter
	{
	public:
		RSEditor(
			ReaShaderController* controller,
			UTF8StringPtr templateName,
			UTF8StringPtr xmlFile
		)
			: VST3Editor(
				controller,
				templateName,
				xmlFile
			),
			_rsController(controller)
		{}


		// SETTERS

		void setSubController(RSUIController* rsSubController) { _subController = rsSubController; }

	protected:

		// ------ from CommandMenuItemTargetAdapter --------

		bool validateCommandMenuItem(CCommandMenuItem* item) override {

			return VST3Editor::validateCommandMenuItem(item);
		}
		bool onCommandMenuItemSelected(CCommandMenuItem* item) override {

			return VST3Editor::onCommandMenuItemSelected(item);
		}
		//--------------------------

		void onMouseEvent(MouseEvent& event, CFrame* frame) override {

			VST3Editor::onMouseEvent(event, frame);
		}

	private:
		// upper references
		RSUIController* _subController{ nullptr };
		ReaShaderController* _rsController{ nullptr };
	};

}
