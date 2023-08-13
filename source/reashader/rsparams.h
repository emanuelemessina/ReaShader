#pragma once
#include <pluginterfaces/vst/vsttypes.h>

#include <string>

namespace ReaShader
{
	enum ReaShaderParamIds : Steinberg::Vst::ParamID
	{
		uAudioGain,
		uVideoParam,
		uRenderingDevice,

		uNumParams
	};

	enum class ReaShaderParamType
	{
		Slider,
		RadioButton,
		CheckBox,
		Hidden,

		numParamTypes
	};

	static const std::string paramTypeStrings[] = { "slider", "radioButton", "checkBox", "hidden" };

	enum class ReaShaderParamGroup
	{
		Main,
		RenderingDeviceSelect,

		numParamGroups
	};

	static const std::string paramGroupStrings[] = { "main", "renderingDeviceSelect" };

#define PARAM_TITLE_MAX_LEN 25
#define PARAM_UNITS_MAX_LEN 8

	struct ReaShaderParameter
	{
		const Steinberg::Vst::TChar title[PARAM_TITLE_MAX_LEN];
		const Steinberg::Vst::TChar units[PARAM_UNITS_MAX_LEN];

		const Steinberg::Vst::ParamValue defaultValue = 0.5f;
		Steinberg::int32 steinbergFlags;

		const ReaShaderParamType type;
		const ReaShaderParamGroup group;

		const Steinberg::Vst::ParamID id;
		Steinberg::Vst::ParamValue value = defaultValue;
	};

	// params objects list
	extern ReaShaderParameter rsParams[uNumParams];
} // namespace ReaShader
