#pragma once

#pragma once

#include "vstgui/lib/iviewlistener.h"
#include "vstgui/uidescription/icontroller.h"

//------------------------------------------------------------------------
namespace Steinberg {
	namespace Vst {

		//------------------------------------------------------------------------
		// AGainUIMessageController
		//------------------------------------------------------------------------
		template <typename ControllerType>
		class RSUIMessageController : public VSTGUI::IController, public VSTGUI::ViewListenerAdapter
		{
		public:
			enum Tags
			{
				kSendMessageTag = 1000
			};

			RSUIMessageController(ControllerType* againController) : againController(againController), textEdit(nullptr)
			{
			}
			~RSUIMessageController() override
			{
				if (textEdit)
					viewWillDelete(textEdit);
				againController->removeUIMessageController(this);
			}

			void setMessageText(String128 msgText)
			{
				if (!textEdit)
					return;
				String str(msgText);
				str.toMultiByte(kCP_Utf8);
				textEdit->setText(str.text8());
			}

		private:
			using CControl = VSTGUI::CControl;
			using CView = VSTGUI::CView;
			using CTextEdit = VSTGUI::CTextEdit;
			using UTF8String = VSTGUI::UTF8String;
			using UIAttributes = VSTGUI::UIAttributes;
			using IUIDescription = VSTGUI::IUIDescription;

			//--- from IControlListener ----------------------
			void valueChanged(CControl* /*pControl*/) override {}
			void controlBeginEdit(CControl* /*pControl*/) override {}
			void controlEndEdit(CControl* pControl) override
			{
				if (pControl->getTag() == kSendMessageTag)
				{
					if (pControl->getValueNormalized() > 0.5f)
					{
						againController->sendTextMessage(textEdit->getText().data());
						pControl->setValue(0.f);
						pControl->invalid();

						//---send a binary message
						if (IPtr<IMessage> message = owned(againController->allocateMessage()))
						{
							message->setMessageID("BinaryMessage");
							uint32 size = 100;
							char8 data[100];
							memset(data, 0, size * sizeof(char));
							// fill my data with dummy stuff
							for (uint32 i = 0; i < size; i++)
								data[i] = i;
							message->getAttributes()->setBinary("MyData", data, size);
							againController->sendMessage(message);
						}
					}
				}
			}
			//--- from IControlListener ----------------------
			//--- is called when a view is created -----
			CView* verifyView(CView* view, const UIAttributes& /*attributes*/,
				const IUIDescription* /*description*/) override
			{
				if (CTextEdit* te = dynamic_cast<CTextEdit*> (view))
				{
					// this allows us to keep a pointer of the text edit view
					textEdit = te;

					// add this as listener in order to get viewWillDelete and viewLostFocus calls
					textEdit->registerViewListener(this);

					// initialize it content
					String str(againController->getDefaultMessageText());
					str.toMultiByte(kCP_Utf8);
					textEdit->setText(str.text8());
				}
				return view;
			}
			//--- from IViewListenerAdapter ----------------------
			//--- is called when a view will be deleted: the editor is closed -----
			void viewWillDelete(CView* view) override
			{
				if (dynamic_cast<CTextEdit*> (view) == textEdit)
				{
					textEdit->unregisterViewListener(this);
					textEdit = nullptr;
				}
			}
			//--- is called when the view is loosing the focus -----------------
			void viewLostFocus(CView* view) override
			{
				if (dynamic_cast<CTextEdit*> (view) == textEdit)
				{
					// save the last content of the text edit view
					const UTF8String& text = textEdit->getText();
					String128 messageText;
					String str;
					str.fromUTF8(text.data());
					str.copyTo(messageText, 0, 128);
					againController->setDefaultMessageText(messageText);
				}
			}
			ControllerType* againController;
			CTextEdit* textEdit;
		};

		//------------------------------------------------------------------------
	} // Vst
} // Steinberg