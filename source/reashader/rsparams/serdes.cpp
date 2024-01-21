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
		// IParameter

		// param id
		if (streamer.writeInt32u(id) == false)
			return false;

		// param title (null terminated)
		if (streamer.writeStr8(title.c_str()) == false)
			return false;

		// reashaderparam group
		if (streamer.writeInt8u((uint8)group) == false)
			return false;

		// param type id
		if (streamer.writeInt8u((uint8)typeId()) == false)
			return false;

		// DerivedParam

		return serializeDerived(streamer);
	}
	bool IParameter::deserialize_v1(TypeInstantiator* ti, IBStreamer& streamer, std::unique_ptr<IParameter>& out)
	{
		out = nullptr;

		// param id
		uint32_t id;
		if (streamer.readInt32u((Steinberg::uint32&)id) == false)
			return false;

		// check end magic
		if (id == -1)
		{
			return true;
		}

		// param title
		std::string title = streamer.readStr8();

		// reashaderparam group
		Group group;
		if (streamer.readInt8u((uint8&)group) == false)
			return false;

		// param type id
		Type typeId;
		if (streamer.readInt8u((uint8&)typeId) == false)
			return false;

		std::unique_ptr<IParameter> tmp = ti->wield(typeId);

		if (tmp == nullptr)
			return false;

		tmp->id = id;
		tmp->title = std::move(title);
		tmp->group = group;

		if (tmp->deserializeDerived_v1(streamer) == false)
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
		// units (null terminated)
		if (streamer.writeStr8(units.c_str()) == false)
			return false;

		// default value
		if (streamer.writeDouble(defaultValue) == false)
			return false;

		// value
		if (streamer.writeDouble(value) == false)
			return false;

		// steinberg flags
		if (streamer.writeInt32(steinbergFlags) == false)
			return false;
	}
	bool VSTParameter::deserializeDerived_v1(IBStreamer& streamer)
	{
		// units
		units = streamer.readStr8();
		// default value
		if (streamer.readDouble(defaultValue) == false)
			return false;
		// value
		if (streamer.readDouble(value) == false)
			return false;
		// steinberg flags
		if (streamer.readInt32(steinbergFlags) == false)
			return false;

		return true;
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
		// value
		if (streamer.writeInt8u(value) == false)
			return false;
		return true;
	}
	bool Int8u::deserializeDerived_v1(IBStreamer& streamer)
	{
		// value
		if (streamer.readInt8u(value) == false)
			return false;
		return true;
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
	{ // value
		if (streamer.writeStr8(value.c_str()) == false)
			return false;
		return true;
	}
	bool String::deserializeDerived_v1(IBStreamer& streamer) 
	{
		// value
		value = streamer.readStr8();

		return true;
	}
}