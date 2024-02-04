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
#include <base/source/fstreamer.h>
#include <string>
#include "tools/logging.h"
#include <functional>
#include <map>
#include "tools/fwd_decl.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

using namespace Steinberg;

// ----------------------

// helpers

// makes the readStr8 steinberg function behave like the other ones
static bool readStr8(IBStreamer& str, std::string& dest)
{
	std::unique_ptr<char8> result(str.readStr8());

	if (result == nullptr)
		return false;
	else
	{
		dest.assign(result.get());
		return true;
	}
}

// ----------------------

namespace ReaShader::Parameters
{
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
			VSTParameter,
			Int8u,
			String,

			numParamTypes
		};

		static const std::string paramTypeStrings[] = { "vstParameter", "int8u", "string" };

		// -----------------------------------

		FWD_DECL(PresetStreamer)
		FWD_DECL(TypeInstantiator)

		// TODO: specific error messages instead of bool

#define IBASEPARAMETER_MEMBER_LIST Steinberg::Vst::ParamID id, std::string title, Group group
#define IBASEPARAMETER_INITIALIZATION IParameter(id, title, group)

		struct IParameter
		{
			IParameter() = default;
			IParameter(IBASEPARAMETER_MEMBER_LIST) : id(id), title(title), group(group){};

			Steinberg::Vst::ParamID id;

			std::string title;

			Group group;

			// every derived param class sets its own type id with a static typeId func
			virtual Type typeId() const = 0;

			bool serialize(IBStreamer& streamer) const;
			static bool deserialize_v1(TypeInstantiator* ti, IBStreamer& streamer, std::unique_ptr<IParameter>& out);
			json toJson() const;
			void fromJson(json& param);

			// used to update the value of the derived param from json
			// if the value provided is not an actual json object, the param takes care of converting the json object into the desierd type
			void setValue(json& newValue)
			{
				setValueFromJson(newValue);
			}
			
			protected:
				
				virtual bool serializeDerived(IBStreamer& streamer) const = 0;
				virtual bool deserializeDerived_v1(IBStreamer& streamer) = 0;

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

		class PresetStreamer
		{
			  public:
				PresetStreamer(IBStream* state) : streamer(state, kLittleEndian)
				{
				}

				void write(std::vector<std::unique_ptr<IParameter>>& rsparams_src,
						   const std::function<void(std::string&&)>& onError);

				void read(std::vector<std::unique_ptr<IParameter>>& rsparams_dst,
						  const std::function<void(std::string&&)>& onError);

				inline IBStreamer& getStreamer()
				{
					return streamer;
				}

			  private:
				IBStreamer streamer;
		};

		// -----------------------------------

		// DO NOT FORGET TO CALL registerInstantiator IN THE TYPEINSTANTIATOR FUNCTION FOR NEW PARAMETER CLASSES
		// INHERIT DEFAULT CONSTRUCTORS WITH  using IParameter::IParameter;

		struct VSTParameter : public IParameter
		{ 
			using IParameter::IParameter;

			VSTParameter(IBASEPARAMETER_MEMBER_LIST, std::string units, Steinberg::Vst::ParamValue defaultValue = 0.5f,
						 Steinberg::Vst::ParamValue value = 0.5f,
						 Steinberg::int32 steinbergFlags = Vst::ParameterInfo::kCanAutomate)
				: IBASEPARAMETER_INITIALIZATION,
				units(units), defaultValue(defaultValue), value(value), steinbergFlags(steinbergFlags){ }

			std::string units;
			Steinberg::Vst::ParamValue defaultValue = 0.5;
			Steinberg::Vst::ParamValue value = 0.5;
			Steinberg::int32 steinbergFlags = Vst::ParameterInfo::kCanAutomate;

			inline void setValueFromJson(json& newValue) override
			{
					newValue.get_to(value);
			}

			inline Type typeId() const override
			{
				return Type::VSTParameter;
			}

			void toJsonDerived(json& j) const override;
			void fromJsonDerived(json& derived) override;
			bool serializeDerived(IBStreamer& streamer) const override;
			bool deserializeDerived_v1(IBStreamer& streamer) override;
		};

		struct Int8u : IParameter
		{
			using IParameter::IParameter;

			Int8u(IBASEPARAMETER_MEMBER_LIST, uint8_t value) : IBASEPARAMETER_INITIALIZATION, value(value)
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
			bool serializeDerived(IBStreamer& streamer) const override;
			bool deserializeDerived_v1(IBStreamer& streamer) override;
		};

		struct String : IParameter
		{
			using IParameter::IParameter;

			String(IBASEPARAMETER_MEMBER_LIST, std::string value = "") : IBASEPARAMETER_INITIALIZATION, value(value)
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
			bool serializeDerived(IBStreamer& streamer) const override;
			bool deserializeDerived_v1(IBStreamer& streamer) override;
		};

		// -----------------------------------

		// default params id list

		enum DefaultParamIds : Steinberg::Vst::ParamID
		{
			uAudioGain,
			uVideoParam,
			uRenderingDevice,
			uCustomShaderName,

			uNumDefaultParams
		};

		static const Type defaultParamTypes[] = { Type::VSTParameter, Type::VSTParameter, Type::Int8u, Type::String };

	}
