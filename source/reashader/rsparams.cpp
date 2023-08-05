#include "rsparams.h"
#include <pluginterfaces/base/fstrdefs.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/vsttypes.h>

using namespace Steinberg;

namespace ReaShader
{
	ReaShaderParameter rsParams[uNumParams] = {
		{ STR16("Audio Gain"), STR16("dB"), 0.5f, Vst::ParameterInfo::kCanAutomate, uAudioGain },
		{ STR16("Video Param"), STR16("units"), 0.5f, Vst::ParameterInfo::kCanAutomate, uVideoParam }
	};
}