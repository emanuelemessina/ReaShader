#pragma once

#include "rseditor.h"
#include "rscontroller.h"
#include "version.h"

namespace ReaShader
{
	using namespace VSTGUI;

	RSUIController::RSUIController(ReaShaderController* controller, VST3Editor* editor,
								   const IUIDescription* description)
		: reaShaderController(controller)
	{
		_ui_url = std::string("http://localhost:") + std::to_string(reaShaderController->rsuiServer->getPort());
	}

	//-------------------------

	//--- from IControlListener ----------------------

	void RSUIController::valueChanged(CControl* pControl)
	{
		/*switch (pControl->getTag()) {
		default: break;
		}*/
	}

	void RSUIController::controlBeginEdit(CControl* pControl)
	{
	}

	void RSUIController::controlEndEdit(CControl* pControl)
	{
		switch (pControl->getTag())
		{
			case tButtonRSUIOpen: {
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
			default:
				break;
		}
	}

	// ---- from IController -----------

	//--- is called when a view will be deleted: the editor is closed -----
	void RSUIController::viewWillDelete(CView* view)
	{
	}

	//--- from IViewListenerAdapter ----------------------
	//--- is called when the view is loosing the focus -----------------
	void RSUIController::viewLostFocus(CView* view)
	{
	}

	//--- is called when a view is created -----
	CView* RSUIController::verifyView(CView* view, const UIAttributes& attributes, const IUIDescription* description)
	{
		std::string uidesc_label = "";

		{
			const std::string* uidesc_label_ptr = attributes.getAttributeValue("uidesc-label");
			if (uidesc_label_ptr)
			{
				uidesc_label = *uidesc_label_ptr;
			}
		}

		if (CViewContainer* container = dynamic_cast<CViewContainer*>(view))
		{
			if (uidesc_label == l_rsui_container)
			{
				rsuiContainer = container;
				container->setTransparency(true);
			}
		}
		else if (CTextButton* button = dynamic_cast<CTextButton*>(view))
		{
			// set tag on rsui-open button
			if (uidesc_label == l_button_rsui_open)
			{
				button->setTag(tButtonRSUIOpen);
				button->setTitle(_ui_url.c_str());
			}
			// bind this as listener to button
			button->setListener(this);
		}
		else if (CTextLabel* label = dynamic_cast<CTextLabel*>(view))
		{
			// set version string label
			if (uidesc_label == l_rs_version)
			{
				label->setText(FULL_VERSION_STR);
			}
		}

		return view;
	}
	//---------------------------------------------

	RSEditor::RSEditor(MyPluginController* controller, UTF8StringPtr templateName, UTF8StringPtr xmlFile)
		: VST3Editor((EditController*)controller, templateName, xmlFile)
	{
	}

	// ------ from CommandMenuItemTargetAdapter --------

	bool RSEditor::validateCommandMenuItem(CCommandMenuItem* item)
	{
		return VST3Editor::validateCommandMenuItem(item);
	}
	bool RSEditor::onCommandMenuItemSelected(CCommandMenuItem* item)
	{
		return VST3Editor::onCommandMenuItemSelected(item);
	}
	//--------------------------

	void RSEditor::onMouseEvent(MouseEvent& event, CFrame* frame)
	{
		VST3Editor::onMouseEvent(event, frame);
	}
} // namespace ReaShader