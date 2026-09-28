/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <functional>
#include <map>
#include <string>

#include "paramstream.h"
#include "tools/fwd_decl.h"
#include "tools/logging.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// ----------------------

namespace ReaShader::Parameters
{
	// stable numeric parameter id -- was Steinberg::Vst::ParamID (a plain uint32 typedef; CLAP's
	// clap_id is the same shape). Kept as its own alias rather than pulling in <clap/id.h> here,
	// since rsparams is otherwise host-format-agnostic and only the CLAP shell layer needs to know
	// this happens to be the same width as clap_id.
	using Id = uint32_t;

	// -----------------------------------

	enum class Group : uint8_t
	{
		Main,
		RenderingDeviceSelect,
		Shader,

		numParamGroups
	};

	static const std::string paramGroupStrings[] = { "main", "renderingDeviceSelect", "shader" };

	// -----------------------------------

	enum class Type : uint8_t // force size to have consistency with serialization
	{
		NumericParameter,
		Int8u,
		String,

		numParamTypes
	};

	static const std::string paramTypeStrings[] = { "numericParameter", "int8u", "string" };

	// -----------------------------------

	FWD_DECL(TypeInstantiator)

	// TODO: specific error messages instead of bool

#define IPARAMETER_MEMBER_LIST Parameters::Id id, std::string title, Group group
#define IPARAMETER_INITIALIZATION IParameter(id, title, group)

	struct IParameter
	{
		IParameter() = default;
		IParameter(IPARAMETER_MEMBER_LIST) : id(id), title(title), group(group){};
		virtual ~IParameter() = default; // required: destroyed polymorphically via unique_ptr<IParameter>

		Parameters::Id id;

		std::string title;

		Group group;

		// every derived param class sets its own type id with a static typeId func
		virtual Type typeId() const = 0;

		bool serialize(ParamWriter& writer) const;
		static bool deserialize_v1(TypeInstantiator* ti, ParamReader& reader, std::unique_ptr<IParameter>& out);
		json toJson() const;
		void fromJson(json& param);

		// used to update the value of the derived param from json
		// if the value provided is not an actual json object, the param takes care of converting the json object into the desierd type
		void setValue(json& newValue)
		{
			setValueFromJson(newValue);
		}

	  protected:
		virtual bool serializeDerived(ParamWriter& writer) const = 0;
		virtual bool deserializeDerived_v1(ParamReader& reader) = 0;

		virtual void toJsonDerived(json& j) const = 0;
		virtual void fromJsonDerived(json& derived) = 0;

		virtual void setValueFromJson(json& newValue) = 0;
	};

	// -----------------------------------

	class TypeInstantiator
	{
	  public:
		std::map<Type, std::unique_ptr<IParameter> (*)()> scepter;

		TypeInstantiator()
		{
			_registerParameterInstantiators();
		}

		inline std::unique_ptr<IParameter> wield(Type type)
		{
			if (scepter.find(type) != scepter.end())
			{
				return scepter[type]();
			}
			else
			{
				return nullptr;
			}
		}

	  private:
		void _registerParameterInstantiators();
		template <typename DerivedParam> inline void _registerParameterInstantiator()
		{
			scepter[DerivedParam{}.typeId()] = []() -> std::unique_ptr<IParameter> {
				return std::make_unique<DerivedParam>();
			};
		}
	};

	inline bool spawnParameter(size_t id, json& props, std::unique_ptr<IParameter>& dst)
	{
		Parameters::Type paramType = (Parameters::Type)props["typeId"];

		if (paramType >= Parameters::Type::numParamTypes)
		{
			return false;
		}

		Parameters::TypeInstantiator ti{};
		std::unique_ptr<Parameters::IParameter> newParam = ti.wield(paramType);
		if (newParam == nullptr) // although it shouldn't because the previous check should've failed
		{
			return false;
		}
		newParam->fromJson(props);

		dst = std::move(newParam);

		return true;
	}

	inline bool spawnParameters(json paramsList, std::vector<std::unique_ptr<IParameter>>& dst,
								 std::function<void(std::unique_ptr<IParameter>&)> beforeMoveToDst)
	{
		dst.clear();
		dst.reserve(paramsList.size());

		for (const auto& item : paramsList)
		{
			for (auto it = item.begin(); it != item.end(); ++it)
			{
				auto id = std::stoi(it.key());
				auto props = it.value();

				std::unique_ptr<Parameters::IParameter> newParam;
				if (!spawnParameter(id, props, newParam))
					return false;

				beforeMoveToDst(newParam);

				dst.push_back(std::move(newParam));
			}
		}

		return true;
	}

	// Serializes/deserializes the whole parameter list to/from a CLAP state stream
	// (clap_plugin_state_t::save/load). Free functions rather than a stateful wrapper class,
	// since CLAP hands save() and load() distinct stream types (unlike VST3's single
	// bidirectional IBStream) -- there's nothing to hold onto between the two.
	namespace Preset
	{
		void write(const clap_ostream_t* stream, std::vector<std::unique_ptr<IParameter>>& rsparams_src,
				   const std::function<void(std::string&&)>& onError);

		void read(const clap_istream_t* stream, std::vector<std::unique_ptr<IParameter>>& rsparams_dst,
				  const std::function<void(std::string&&)>& onError);
	} // namespace Preset

	// -----------------------------------

	// DO NOT FORGET TO CALL registerInstantiator IN THE TYPEINSTANTIATOR FUNCTION FOR NEW PARAMETER CLASSES
	// INHERIT DEFAULT CONSTRUCTORS WITH  using IParameter::IParameter;

	// A host-automatable numeric parameter (normalized [0,1] range, like the rest of this
	// system assumed under VST3). Was "VSTParameter" -- renamed since there's no VST3 left;
	// maps to a CLAP clap_param_info_t with CLAP_PARAM_IS_AUTOMATABLE.
	struct NumericParameter : public IParameter
	{
		using IParameter::IParameter;

		NumericParameter(IPARAMETER_MEMBER_LIST, std::string units, double defaultValue = 0.5, double value = 0.5,
						  bool automatable = true)
			: IPARAMETER_INITIALIZATION, units(units), defaultValue(defaultValue), value(value),
			  automatable(automatable)
		{
		}

		std::string units;
		double defaultValue = 0.5;
		double value = 0.5;
		bool automatable = true;

		inline void setValueFromJson(json& newValue) override
		{
			newValue.get_to(value);
		}

		inline Type typeId() const override
		{
			return Type::NumericParameter;
		}

		void toJsonDerived(json& j) const override;
		void fromJsonDerived(json& derived) override;
		bool serializeDerived(ParamWriter& writer) const override;
		bool deserializeDerived_v1(ParamReader& reader) override;
	};

	struct Int8u : IParameter
	{
		using IParameter::IParameter;

		Int8u(IPARAMETER_MEMBER_LIST, uint8_t value) : IPARAMETER_INITIALIZATION, value(value)
		{
		}

		uint8_t value;

		inline void setValueFromJson(json& newValue) override
		{
			newValue.get_to(value);
		}

		inline Type typeId() const override
		{
			return Type::Int8u;
		}

		void toJsonDerived(json& j) const override;
		void fromJsonDerived(json& derived) override;
		bool serializeDerived(ParamWriter& writer) const override;
		bool deserializeDerived_v1(ParamReader& reader) override;
	};

	struct String : IParameter
	{
		using IParameter::IParameter;

		String(IPARAMETER_MEMBER_LIST, std::string value = "") : IPARAMETER_INITIALIZATION, value(value)
		{
		}

		std::string value{ "" };

		inline void setValueFromJson(json& newValue) override
		{
			newValue.get_to(value);
		}

		inline Type typeId() const override
		{
			return Type::String;
		}

		void toJsonDerived(json& j) const override;
		void fromJsonDerived(json& derived) override;
		bool serializeDerived(ParamWriter& writer) const override;
		bool deserializeDerived_v1(ParamReader& reader) override;
	};

	struct ShaderParameter : NumericParameter
	{
		using NumericParameter::NumericParameter;

		ShaderParameter(IPARAMETER_MEMBER_LIST, std::string parentStructName, std::string parentVectorName,
						std::string units, double defaultValue = 0.5, double value = 0.5, bool automatable = true)
			: NumericParameter(id, title, group, units, defaultValue, value, automatable),
			  parentStructName(parentStructName), parentVectorName(parentVectorName)
		{
		}

		// always non empty, shader params are always bound to struct
		std::string parentStructName;
		// specified if component of a vector
		std::string parentVectorName;
	};

	// -----------------------------------

	// default params id list

	enum DefaultParamIds : Parameters::Id
	{
		uAudioGain,
		uVideoParam,
		uRenderingDevice,
		uCustomShaderName,

		uNumDefaultParams
	};

	static const Type defaultParamTypes[] = { Type::NumericParameter, Type::NumericParameter, Type::Int8u,
											   Type::String };

} // namespace ReaShader::Parameters
