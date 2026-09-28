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
		| IParameter fields | param type id (int8) | derived struct serialization |
	| end magic (uint32) = -1 |

	Parameter members order:

	struct IParameter
	{
		Parameters::Id id; // uint32
		std::string title;
		Group group;
	}
	struct NumericParameter : IParameter
	{
		std::string units;
		double defaultValue = 0.5;
		double value = defaultValue;
		bool automatable = true;
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
		id = param["id"];
		title = param["title"];
		group = (Parameters::Group)param["groupId"];

		fromJsonDerived(param);
	}
	bool IParameter::serialize(ParamWriter& writer) const
	{
		return
			// IParameter

			// param id
			writer.writeInt32u(id) &&
			// param title (null terminated)
			writer.writeStr8(title) &&
			// reashaderparam group
			writer.writeInt8u((uint8_t)group) &&
			// param type id
			writer.writeInt8u((uint8_t)typeId()) &&

			// DerivedParam
			serializeDerived(writer);
	}
	bool IParameter::deserialize_v1(TypeInstantiator* ti, ParamReader& reader, std::unique_ptr<IParameter>& out)
	{
		out = nullptr;

		// param id
		uint32_t id;
		if (!reader.readInt32u(id))
			return false;

		// check end magic
		if (id == (uint32_t)-1)
		{
			return true;
		}

		// param title
		std::string title;
		if (!reader.readStr8(title))
			return false;

		// reashaderparam group
		uint8_t group;
		if (!reader.readInt8u(group))
			return false;

		// param type id
		uint8_t typeId;
		if (!reader.readInt8u(typeId))
			return false;

		std::unique_ptr<IParameter> tmp = ti->wield((Type)typeId);

		if (tmp == nullptr)
			return false;

		tmp->id = id;
		tmp->title = std::move(title);
		tmp->group = (Group)group;

		if (!tmp->deserializeDerived_v1(reader))
			return false;

		out = std::move(tmp);

		return true;
	}

	// ----------------------

	// NumericParameter

	void NumericParameter::toJsonDerived(json& j) const
	{
		j["units"] = units;
		j["defaultValue"] = defaultValue;
		j["value"] = value;
	}
	void NumericParameter::fromJsonDerived(json& derived)
	{
		units = derived["units"];
		defaultValue = derived["defaultValue"];
		value = derived["value"];
	}
	bool NumericParameter::serializeDerived(ParamWriter& writer) const
	{
		return
			// units (null terminated)
			writer.writeStr8(units) &&
			// default value
			writer.writeDouble(defaultValue) &&
			// value
			writer.writeDouble(value) &&
			// automatable flag
			writer.writeInt8u(automatable ? 1 : 0);
	}

	bool NumericParameter::deserializeDerived_v1(ParamReader& reader)
	{
		uint8_t automatableByte = 1;
		bool ok =
			// units
			reader.readStr8(units) &&
			// default value
			reader.readDouble(defaultValue) &&
			// value
			reader.readDouble(value) &&
			// automatable flag
			reader.readInt8u(automatableByte);
		automatable = automatableByte != 0;
		return ok;
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
	bool Int8u::serializeDerived(ParamWriter& writer) const
	{
		return
			// value
			writer.writeInt8u(value);
	}
	bool Int8u::deserializeDerived_v1(ParamReader& reader)
	{
		return
			// value
			reader.readInt8u(value);
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
	bool String::serializeDerived(ParamWriter& writer) const
	{
		return
			// value
			writer.writeStr8(value);
	}
	bool String::deserializeDerived_v1(ParamReader& reader)
	{
		return
			// value
			reader.readStr8(value);
	}
} // namespace ReaShader::Parameters
