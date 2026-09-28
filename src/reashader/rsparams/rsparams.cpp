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

		_registerParameterInstantiator<VSTParameter>();
		_registerParameterInstantiator<Int8u>();
		_registerParameterInstantiator<String>();
	}

	void PresetStreamer::write(std::vector<std::unique_ptr<IParameter>>& rsparams_src,
							 const std::function<void(std::string&& msg)>& onError)
	{
		// preset format version number
		if (!streamer.writeInt8u(1))
			onError("Error trying to write preset (start)!");
		
		// make sure everything is saved in order

		int i = 0;
		for (; i < rsparams_src.size(); i++)
		{
			if (!rsparams_src[i]->serialize(streamer))
				break;
		}

		if (i < rsparams_src.size())
		{
			onError(std::format("Error writing param {} with id {}. \nWriting stopped abruptly.", rsparams_src[i]->title, i));
		}

		// end magic
		if (!streamer.writeInt32u(-1))
			onError("Error trying to write preset (end)!");
	}

	void PresetStreamer::read(std::vector<std::unique_ptr<IParameter>>& rsparams_dst,
							  const std::function<void(std::string&& msg)>& onError)
	{
		// preset format version number switch
		uint8_t versionNumber;
		if (!streamer.readInt8u(versionNumber))
			onError("Error trying to read preset (start)!");

		// select deserializer version

		std::function<bool(TypeInstantiator*, IBStreamer&, std::unique_ptr<IParameter>&)> deserializer;
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

		int i = 0;
		for (; true; i++) // if end magic is malformed, the other checks will fail anyway
		{
			std::unique_ptr<IParameter> newParam = nullptr;
			if (!deserializer(&ti, streamer, newParam)) 
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
			if (newParam->typeId() != defaultParamTypes[i])
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
			onError(std::format("Error reading param {} with id {}.\nPreset loading refuted.", rsparams_dst[i]->title, i));
			return;
		}

		// replace
		rsparams_dst = std::move(tmp_params);
	}
}