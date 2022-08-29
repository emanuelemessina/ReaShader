#pragma once

#include "vstgui/plugin-bindings/vst3editor.h"

namespace ReaShader {

	using namespace VSTGUI;

	class RSEditor : public VST3Editor {
	public:
		RSEditor(
			Steinberg::Vst::EditController* controller,
			UTF8StringPtr templateName,
			UTF8StringPtr xmlFile
		)
			: VST3Editor(
				controller,
				templateName,
				xmlFile
			) {}

	};

}
