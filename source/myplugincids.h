//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#pragma once

#ifndef MYPLUGINCIDS_H
#define MYPLUGINCIDS_H

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"


namespace ReaShader {
	//------------------------------------------------------------------------
	static const Steinberg::FUID kReaShaderProcessorUID(0xE0F1D00D, 0xC00B5117, 0x82AD083B, 0xE3690071);
	static const Steinberg::FUID kReaShaderControllerUID(0xBA5B5281, 0x9C3559EA, 0xAC0EB279, 0xF509F17C);

#define ReaShaderVST3Category "Fx"

	//------------------------------------------------------------------------

	// Params (with defaults)

	enum ReaShaderParamIds : Steinberg::Vst::ParamID {
		uAudioGain,
		uVideoParam,

		uNumParams
	};

} // namespace ReaShader

#endif