#pragma once

#include "rsuieditor.h"
#include "rscontroller.h"
#include "version.h"

namespace ReaShader
{
	using namespace VSTGUI;

	namespace RSUI
	{

		RSUISubController::RSUISubController(ReaShaderController* controller, VST3Editor* editor,
											 const IUIDescription* description)
			: reaShaderController(controller)
		{
			_ui_url = std::string("http://localhost:") + std::to_string(reaShaderController->rsuiServer->getPort());
		}

		//-------------------------

		//--- from IControlListener ----------------------

		void RSUISubController::valueChanged(CControl* pControl)
		{
			/*switch (pControl->getTag()) {
			default: break;
			}*/
		}

		void RSUISubController::controlBeginEdit(CControl* pControl)
		{
		}

		void RSUISubController::controlEndEdit(CControl* pControl)
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
		void RSUISubController::viewWillDelete(CView* view)
		{
		}

		//--- from IViewListenerAdapter ----------------------
		//--- is called when the view is loosing the focus -----------------
		void RSUISubController::viewLostFocus(CView* view)
		{
		}

		//--- is called when a view is created -----
		CView* RSUISubController::verifyView(CView* view, const UIAttributes& attributes,
											 const IUIDescription* description)
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

		RSUIEditor::RSUIEditor(MyPluginController* controller, UTF8StringPtr templateName, UTF8StringPtr xmlFile)
			: VST3Editor((EditController*)controller, templateName, xmlFile)
		{
		}

		// ------ from CommandMenuItemTargetAdapter --------

		bool RSUIEditor::validateCommandMenuItem(CCommandMenuItem* item)
		{
			return VST3Editor::validateCommandMenuItem(item);
		}
		bool RSUIEditor::onCommandMenuItemSelected(CCommandMenuItem* item)
		{
			return VST3Editor::onCommandMenuItemSelected(item);
		}
		//--------------------------

		void RSUIEditor::onMouseEvent(MouseEvent& event, CFrame* frame)
		{
			VST3Editor::onMouseEvent(event, frame);
		}

	} // namespace RSUI

} // namespace ReaShader