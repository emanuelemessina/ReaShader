/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#include "rsparams.h"

namespace ReaShader::Parameters
{
	// for each new parameter class, we need to register its instantiator to allow for deserialization from typeid
	void TypeInstantiator::_registerParameterInstantiators()
	{
		scepter.clear();

		// register the parameter instantiators

		_registerParameterInstantiator<NumericParameter>();
		_registerParameterInstantiator<Int8u>();
		_registerParameterInstantiator<String>();
	}

	namespace Preset
	{
		void write(const clap_ostream_t* stream, std::vector<std::unique_ptr<IParameter>>& rsparams_src,
				   const std::function<void(std::string&& msg)>& onError)
		{
			ParamWriter writer{ stream };

			// preset format version number
			if (!writer.writeInt8u(1))
				onError("Error trying to write preset (start)!");

			// make sure everything is saved in order

			size_t i = 0;
			for (; i < rsparams_src.size(); i++)
			{
				if (!rsparams_src[i]->serialize(writer))
					break;
			}

			if (i < rsparams_src.size())
			{
				onError(
					std::format("Error writing param {} with id {}. \nWriting stopped abruptly.", rsparams_src[i]->title, i));
			}

			// end magic
			if (!writer.writeInt32u((uint32_t)-1))
				onError("Error trying to write preset (end)!");
		}

		void read(const clap_istream_t* stream, std::vector<std::unique_ptr<IParameter>>& rsparams_dst,
				  const std::function<void(std::string&& msg)>& onError)
		{
			ParamReader reader{ stream };

			// preset format version number switch
			uint8_t versionNumber;
			if (!reader.readInt8u(versionNumber))
				onError("Error trying to read preset (start)!");

			// select deserializer version

			std::function<bool(TypeInstantiator*, ParamReader&, std::unique_ptr<IParameter>&)> deserializer;
			switch (versionNumber)
			{
				case 1:
					deserializer = &IParameter::deserialize_v1;
					break;

				default:
					onError(std::format("Unknown preset file version number: {}.\nPreset loading refuted.", versionNumber));
					return;
			}

			// proceed with deserialization

			std::vector<std::unique_ptr<IParameter>> tmp_params;

			TypeInstantiator ti{};

			bool error = true;

			size_t i = 0;
			for (; true; i++) // if end magic is malformed, the other checks will fail anyway
			{
				std::unique_ptr<IParameter> newParam = nullptr;
				if (!deserializer(&ti, reader, newParam))
				{
					break; // failure, error is true
				}
				else if (newParam == nullptr) // end
				{
					error = false; // success, clear error
					break;
				}

				// deser ok

				// default params config check (1) (corrupted file or different plugin version)
				// check type mismatch
				if (i < Parameters::uNumDefaultParams && newParam->typeId() != defaultParamTypes[i])
				{
					onError(std::format("Preset config Mismatch\n\nThe default parameters config in the loaded preset does not match the current one.\nPreset loading refuted."));
					return;
				}

				// add param to dst vector
				tmp_params.push_back(std::move(newParam));
			}

			// default params config check (2)
			// default part inferior size
			if (tmp_params.size() < Parameters::uNumDefaultParams)
			{
				onError(std::format("Preset config Mismatch\n\nThe default parameters config in the loaded preset does not match the current one.\nPreset loading refuted."));
				return;
			}

			if (error)
			{
				onError(std::format("Error reading param {}.\nPreset loading refuted.", i));
				return;
			}

			// replace
			rsparams_dst = std::move(tmp_params);
		}
	} // namespace Preset
} // namespace ReaShader::Parameters
