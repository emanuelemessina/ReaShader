/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "rsparams.h"

// v1

/*
	RSPreset v1 format:

	| preset version number (uint8) |
	[list of serialized parameters]
		| IBaseParam deserialization | param type id (int8) | derived struct serialization |
	| end magic (uint32) = -1 |

	Parameter members order:

	struct IParameter
	{
		Steinberg::Vst::ParamID id;

		std::string title;

		Group group;
	}
	struct VSTParameter : IParameter
	{
		std::string units;
		Steinberg::Vst::ParamValue defaultValue = 0.5f;
		Steinberg::Vst::ParamValue value = defaultValue;
		Steinberg::int32 steinbergFlags = Vst::ParameterInfo::kCanAutomate;
	}
	struct Int8u : IParameter
	{
		uint8_t value;
	}
	struct String : IParameter
	{
		std::string value;
	}

*/

// ----------------------

namespace ReaShader::Parameters
{
	// IParameter

	json IParameter::toJson() const
	{
		json j;

		j["id"] = id;
		j["title"] = title;
		j["groupId"] = (uint8_t)group;
		j["group"] = paramGroupStrings[(uint8_t)group];
		j["typeId"] = (uint8_t)typeId();
		j["type"] = paramTypeStrings[(uint8_t)typeId()];
		
		toJsonDerived(j);

		return j;
	}
	void IParameter::fromJson(json& param)
	{
		title = param["title"];
		group = (Parameters::Group)param["groupId"];

		fromJsonDerived(param["derived"]);
	}
	bool IParameter::serialize(IBStreamer& streamer) const
	{
		return
		// IParameter

		// param id
		streamer.writeInt32u(id) &&
		// param title (null terminated)
		streamer.writeStr8(title.c_str()) &&
		// reashaderparam group
		streamer.writeInt8u((uint8)group) &&
		// param type id
		streamer.writeInt8u((uint8)typeId()) &&
		
		// DerivedParam
		serializeDerived(streamer);
	}
	bool IParameter::deserialize_v1(TypeInstantiator* ti, IBStreamer& streamer, std::unique_ptr<IParameter>& out)
	{
		out = nullptr;

		// param id
		uint32_t id;
		if (!streamer.readInt32u((Steinberg::uint32&)id))
			return false;

		// check end magic
		if (id == -1)
		{
			return true;
		}

		// param title
		std::string title;
		if (!readStr8(streamer, title))
			return false;

		// reashaderparam group
		Group group;
		if (!streamer.readInt8u((uint8&)group))
			return false;

		// param type id
		Type typeId;
		if (!streamer.readInt8u((uint8&)typeId))
			return false;

		std::unique_ptr<IParameter> tmp = ti->wield(typeId);

		if (tmp == nullptr)
			return false;

		tmp->id = id;
		tmp->title = std::move(title);
		tmp->group = group;

		if (!tmp->deserializeDerived_v1(streamer))
			return false;

		out = std::move(tmp);

		return true;
	}

// ----------------------

	// VSTParameter

	void VSTParameter::toJsonDerived(json& j) const
	{
		j["units"] = units;
		j["defaultValue"] = defaultValue;
		j["value"] = value;
	}
	void VSTParameter::fromJsonDerived(json& derived)
	{
		units = derived["units"];
		defaultValue = derived["defaultValue"];
		value = derived["value"];
	}
	bool VSTParameter::serializeDerived(IBStreamer& streamer) const
	{
		return
		// units (null terminated)
		streamer.writeStr8(units.c_str()) &&
		// default value
		streamer.writeDouble(defaultValue) &&
		// value
		streamer.writeDouble(value) &&
		// steinberg flags
		streamer.writeInt32(steinbergFlags)
		;
	}
	
	bool VSTParameter::deserializeDerived_v1(IBStreamer& streamer)
	{
		return
		// units
		 readStr8(streamer, units) &&
		// default value
		streamer.readDouble(defaultValue) &&
		// value
		streamer.readDouble(value) &&
		// steinberg flags
		streamer.readInt32(steinbergFlags) 
		;
	}
		

	// Int8u

	void Int8u::toJsonDerived(json& j) const
	{
		j["value"] = value;
	}
	void Int8u::fromJsonDerived(json& derived)
	{
		value = derived["value"];
	}
	bool Int8u::serializeDerived(IBStreamer& streamer) const
	{
		return
		// value
		streamer.writeInt8u(value)
		;
	}
	bool Int8u::deserializeDerived_v1(IBStreamer& streamer)
	{
		return
		// value
		streamer.readInt8u(value)
		;
	}

	// String

	void String::toJsonDerived(json& j) const
	{
		j["value"] = value;
	}
	void String::fromJsonDerived(json& derived)
	{
		value = derived["value"];
	}
	bool String::serializeDerived(IBStreamer& streamer) const 
	{ 
		return
		// value
		streamer.writeStr8(value.c_str())
		;
	}
	bool String::deserializeDerived_v1(IBStreamer& streamer) 
	{
		return
		// value
		readStr8(streamer, value)
		;
	}
}