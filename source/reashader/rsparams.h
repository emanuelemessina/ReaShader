/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once
#include <pluginterfaces/base/fstrdefs.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <pluginterfaces/vst/vsttypes.h>
#include <string>

using namespace Steinberg;

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

	struct ReaShaderParameter
	{
		std::wstring title;
		std::wstring units;

		Steinberg::Vst::ParamValue defaultValue = 0.5f;
		Steinberg::int32 steinbergFlags;

		ReaShaderParamType type;
		ReaShaderParamGroup group;

		Steinberg::Vst::ParamID id;
		Steinberg::Vst::ParamValue value = defaultValue;
	};

	// params objects list
	static const ReaShaderParameter defaultRSParams[uNumParams] = {
		{ STR16("Audio Gain"), STR16("%"), 0.5f, Vst::ParameterInfo::kCanAutomate, ReaShaderParamType::Slider,
		  ReaShaderParamGroup::Main, uAudioGain },
		{ STR16("Video Param"), STR16("%"), 0.5f, Vst::ParameterInfo::kCanAutomate, ReaShaderParamType::Slider,
		  ReaShaderParamGroup::Main, uVideoParam },
		{ STR16("Rendering Device"), STR16(""), 0, Vst::ParameterInfo::kIsReadOnly, ReaShaderParamType::Hidden,
		  ReaShaderParamGroup::RenderingDeviceSelect, uRenderingDevice }
	};
} // namespace ReaShader
