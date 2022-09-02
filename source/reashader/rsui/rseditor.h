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

namespace ReaShader {

	using namespace VSTGUI;

	enum RSUITags {
		tMenuButton,
		tSplashScreen
	};

	class RSSplashScreen :
		public VSTGUI::ViewListenerAdapter
	{
	public:

		RSSplashScreen(CViewContainer* parent, const CRect& size, const CRect& sizeContent, IControlListener* listener, int32_t tag)
			: parent(parent), sizeContent(sizeContent) {

			// wrapper view
			splashContainer = new CViewContainer(size);
			splashContainer->registerViewListener(this);

			splashScreen = new CSplashScreen(size, listener, tag, splashContainer);
			splashScreen->setDisplayArea(size);

			parent->addView(splashScreen);
		}

		/**
		 * Adds view to splashContainer.
		 */
		bool addView(CView* view) {
			return splashContainer->addView(view);
		}

		CViewContainer* const getContainer() const {
			return splashContainer;
		}

		void splash() {
			MouseDownEvent event{ {0,0},MouseButton::Left };
			CPoint where(0, 0);
			splashScreen->onMouseDown(where, kLButton);
			splashed = true;
		}

		bool isSplashed() {
			return splashed;
		}

		CRect getSizeContent() {
			return sizeContent;
		}

		void destroy(IViewMouseListener* mouseListener) {
			// unregister mouse listeners for container children
			if (mouseListener) {
				splashContainer->forEachChild([&](CView* child) {
					child->unregisterViewMouseListener(mouseListener);
					});
			}

			// unregister view listener for container
			splashContainer->unregisterViewListener(this);

			// close splash
			splashScreen->unSplash();

			splashed = false;

			parent->removeView(splashScreen);
		}

	protected:

		//--- from IViewListenerAdapter ----------------------

		//--- is called when a view will be deleted: the editor is closed -----
		void viewWillDelete(CView* view) override
		{
			if (dynamic_cast<CTextEdit*> (view) == nullptr)
			{
				/*textEdit->unregisterViewListener(this);
				textEdit = nullptr;*/
			}
		}
		//--- is called when the view is loosing the focus -----------------
		void viewLostFocus(CView* view) override
		{
			if (dynamic_cast<CTextEdit*> (view) == nullptr)
			{
				// save the last content of the text edit view
				/*const UTF8String& text = textEdit->getText();
				String128 messageText;
				String str;
				str.fromUTF8(text.data());
				str.copyTo(messageText, 0, 128);
				againController->setDefaultMessageText(messageText);*/
			}
		}

	private:

		CViewContainer* parent{ nullptr };
		CSplashScreen* splashScreen{ nullptr };
		CViewContainer* splashContainer{ nullptr };

		bool splashed{ false };
		CRect sizeContent{};
	};

	class RSUIController
		: public VSTGUI::IController,
		public VSTGUI::ViewListenerAdapter
	{
	public:

		RSUIController(
			Steinberg::Vst::EditController* controller,
			VST3Editor* editor,
			const IUIDescription* description
		)
			: controller(controller), editor(editor)
		{}

		//-------------------------

		//--- from IControlListener ----------------------

		void valueChanged(CControl* pControl) override {
			switch (pControl->getTag()) {

			}
		}

		void controlBeginEdit(CControl* pControl) override {
			switch (pControl->getTag()) {
				// menu button clicked
			case tMenuButton:

				break;
			}
		}

		void controlEndEdit(CControl* pControl) override
		{
			//if (pControl->getTag() == kSendMessageTag)
			//{
			//	if (pControl->getValueNormalized() > 0.5f)
			//	{
			//		againController->sendTextMessage(textEdit->getText().data());
			//		pControl->setValue(0.f);
			//		pControl->invalid();

			//		//---send a binary message
			//		if (IPtr<IMessage> message = owned(againController->allocateMessage()))
			//		{
			//			message->setMessageID("BinaryMessage");
			//			uint32 size = 100;
			//			char8 data[100];
			//			memset(data, 0, size * sizeof(char));
			//			// fill my data with dummy stuff
			//			for (uint32 i = 0; i < size; i++)
			//				data[i] = i;
			//			message->getAttributes()->setBinary("MyData", data, size);
			//			againController->sendMessage(message);
			//		}
			//	}
			//}
		}

		// ---- from IController -----------

		const std::string l_rsui_container = "rsui";


		//--- is called when a view is created -----
		CView* verifyView(CView* view, const UIAttributes& attributes,
			const IUIDescription* description) override
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

					CRect size = container->getViewSize();
					CPoint origin{};
					attributes.getPointAttribute("origin", origin);

					//CView* imageView = new CView({ 0, 0, 100, 100 });
					//{
					//	std::string bPath = tools::paths::join({ "assets","images","reashader text.png" });
					//	CResourceDescription bDesc(bPath.c_str());
					//	CBitmap* logo = new CBitmap(bDesc);
					//	imageView->setBackground(logo);
					//}

					//CTextButton* button = new CTextButton({ 0, 0, 100, 100 }, this, 777, "hey");

					////container->addView(imageView);

					//CTextLabel* label = new CTextLabel(size);
					//label->setText("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
					//label->setTransparency(true);
					////container->addView(label, imageView);

				}
			}
			else if (COptionMenu* optionMenu = dynamic_cast<COptionMenu*> (view)) {
				menu = optionMenu;
				menu->setTransparency(true);

				{
					CCommandMenuItem::Desc desc("Contact developer", editor, "Help", "Contact developer");
					CMenuItem* item = new CCommandMenuItem(desc);
					menu->addEntry(item);
				}

				{
					CCommandMenuItem::Desc desc("Credits", editor, "Help", "Credits");
					CMenuItem* item = new CCommandMenuItem(desc);
					menu->addEntry(item);
				}

			}
			else if (CTextButton* button = dynamic_cast<CTextButton*> (view)) {
				button->setTag(tMenuButton);
				button->setListener(this);
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

		// upper references
		Steinberg::Vst::EditController* controller;
		VST3Editor* editor;

		// item of subcontroller but not in main subcontainer
		COptionMenu* menu{ nullptr };

		// main container of subcontroller
		CViewContainer* rsuiContainer{ nullptr };

		// children of main container

	};

	static inline void alignHorizontal(CRect& rect, CRect& control) {
		rect.offset(control.getCenter().x - rect.getCenter().x, 0);
	}

	static inline CTextLabel* makeRsLabel(const CRect& size, const char* txt, CHoriTxtAlign hAlign = CHoriTxtAlign::kLeftText) {
		CTextLabel* label = new CTextLabel(size, (UTF8StringPtr)txt);
		label->setBackColor({ 0,0,0,0 });
#ifdef DEBUG
		label->setFrameWidth(2);
		label->setFrameColor(kRedCColor);
#endif
		label->setFontColor(kWhiteCColor);
		label->setHoriAlign(hAlign);
		return label;
	}

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
			controller(controller)
		{}

		// upper references
		RSUIController* subController{ nullptr };
		ReaShaderController* controller{ nullptr };

	protected:

		// ------ from CommandMenuItemTargetAdapter --------

		bool validateCommandMenuItem(CCommandMenuItem* item) override {
			if (item->getCommandCategory() == "Help") {
				return false;
			}
			return VST3Editor::validateCommandMenuItem(item);
		}
		bool onCommandMenuItemSelected(CCommandMenuItem* item) override {
			if (item->getCommandCategory() == "Help") {
				if (item->getCommandName() == "Credits") {

					// splash size entire window
					CRect splashRect(subController->rsuiContainer->getViewSize());
					// content size
					CRect scrollViewSize = splashRect;
					scrollViewSize.inset(70, 70);

					// create splash
					splash = new RSSplashScreen(subController->rsuiContainer, splashRect, scrollViewSize, subController, tSplashScreen);

					splash->getContainer()->setBackgroundColor({ 0,0,0,100 });

					// views

					// relative to scrollview
					CRect scrollContainerSize = { 0,0,scrollViewSize.getWidth(), scrollViewSize.getHeight() };
					scrollContainerSize.setHeight(scrollViewSize.getHeight());
					scrollContainerSize.inset(20, 20);

					int32_t scrollViewFlags = CScrollView::CScrollViewStyle::kAutoHideScrollbars | CScrollView::CScrollViewStyle::kVerticalScrollbar | CScrollView::CScrollViewStyle::kOverlayScrollbars;
					CScrollView* scrollViewCredits = new CScrollView(scrollViewSize, scrollContainerSize, scrollViewFlags, 10);
					scrollViewCredits->registerViewMouseListener(this);

					splash->addView(scrollViewCredits);

					// populate scrollview

					// relative to scrollcontainer
					CRect scrollContainerSizeRelative = { 0,0,scrollContainerSize.getWidth(), scrollContainerSize.getHeight() };

					CRect textLabelSize = scrollContainerSizeRelative;
					int textLabelHeight = 50;
					textLabelSize.setHeight(textLabelHeight).setWidth(textLabelSize.getWidth() / 3);

					CRect ts0 = textLabelSize;
					alignHorizontal(ts0, scrollContainerSizeRelative);
					scrollViewCredits->addView(makeRsLabel(ts0, "CREDITS", kCenterText));

					CRect ts1 = textLabelSize.offset(0, textLabelHeight);
					scrollViewCredits->addView(makeRsLabel(ts1, "Developer: "));

					CRect ts1_r = ts1;
					ts1_r.setWidth(scrollContainerSizeRelative.getWidth() - ts1.getWidth()).offset(ts1.getWidth(), 0);
					scrollViewCredits->addView(makeRsLabel(ts1_r, "Emanuele Messina", kRightText));
					scrollViewCredits->addView(makeRsLabel(ts1_r.offset(0, textLabelHeight), "( github.com/emanuelemessina )", kRightText));

					CRect ts2 = ts1.offset(0, textLabelHeight * 2);
					scrollViewCredits->addView(makeRsLabel(ts2, "Third Party: "));

					scrollViewCredits->addView(makeRsLabel(ts1_r.offset(0, textLabelHeight), "VST by Steiberg Media Technologies GmbH", kRightText));
					scrollViewCredits->addView(makeRsLabel(ts1_r.offset(0, textLabelHeight), "Vulkan by Khronos Group", kRightText));

					CRect ts3 = ts2.offset(0, textLabelHeight * 2);
					scrollViewCredits->addView(makeRsLabel(ts3, "Open Source: "));

					scrollViewCredits->addView(makeRsLabel(ts1_r.offset(0, textLabelHeight), "GLM, STB, cwalk, Tiny Obj Loader", kRightText));
					scrollViewCredits->addView(makeRsLabel(ts1_r.offset(0, textLabelHeight), "WDL, Vulkan Memory Allocator", kRightText));

					// extend scroll container height

					CRect extContainerSize = scrollContainerSize.setHeight(textLabelHeight * 7);
					scrollViewCredits->setContainerSize(extContainerSize);

					// open splash
					splash->splash();

				}
				return false;
			}
			return VST3Editor::onCommandMenuItemSelected(item);
		}
		//--------------------------

		void onMouseEvent(MouseEvent& event, CFrame* frame) override {
			// check splash
			if (splash && splash->isSplashed() && event.buttonState.isLeft() && event.type == EventType::MouseDown) {
				if (!splash->getSizeContent().pointInside(event.mousePosition)) {
					splash->destroy(this);
				}
			}

			VST3Editor::onMouseEvent(event, frame);
		}

	private:
		// splash
		RSSplashScreen* splash{ nullptr };
	};

}
