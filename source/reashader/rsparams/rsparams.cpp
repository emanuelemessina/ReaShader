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
							 const std::function<void(int faultIndex)>& onError)
	{
		// preset format version number
		streamer.writeInt8u(1);

		// make sure everything is saved in order

		int i = 0;
		for (; i < rsparams_src.size(); i++)
		{
			if (rsparams_src[i]->serialize(streamer) == false)
				break;
		}

		// end magic
		streamer.writeInt32u(-1);

		if (i < rsparams_src.size())
		{
			onError(i);
		}
	}

	void PresetStreamer::read(std::vector<std::unique_ptr<IParameter>>& rsparams_dst, const std::function<void(int faultIndex)>& onError)
	{
		// preset format version number switch
		uint8_t versionNumber;
		streamer.readInt8u(versionNumber);

		// select deserializer version

		std::function<bool(TypeInstantiator*, IBStreamer&, std::unique_ptr<IParameter>&)> deserializer;
		switch (versionNumber)
		{
			case 1:
				deserializer = &IParameter::deserialize_v1;
				break;

			default:
				LOG(WARNING, toConsole | toFile | toBox, "RSPresetStreamer", "Preset File reading Error",
					std::format("Unknown version number {}", versionNumber));
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
			if (deserializer(&ti, streamer, newParam) == false)
			{
				break;
			}
			else if (newParam == nullptr) // end
			{
				error = false;
				break;
			}

			// default params config check (1) (corrupted file or different plugin version)
			// check type mismatch
			if (newParam->typeId() != defaultParamTypes[i])
			{
				LOG(WARNING, toConsole | toFile | toBox, "RSPresetStreamer", "Preset reading Mismatch",
					std::format("The default parameters config in the loaded preset does not match the current one.\n Preset loading refuted.", versionNumber));
				return;
			}

			// add param to dst vector
			tmp_params.push_back(std::move(newParam));
		}

		// default params config check (2)
		// default part inferior size
		if (tmp_params.size() < Parameters::uNumDefaultParams)
		{
			LOG(WARNING, toConsole | toFile | toBox, "RSPresetStreamer", "Preset reading Mismatch",
				std::format("The default parameters config in the loaded preset does not match the current one.\n "
							"Preset loading refuted.",
							versionNumber));
			return;
		}

		if (error)
		{
			onError(i);
			return;
		}

		// replace
		rsparams_dst = std::move(tmp_params);
	}
}