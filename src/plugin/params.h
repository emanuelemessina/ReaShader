/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace ReaShader::Parameters
{
	using json = nlohmann::json;
	using Id = uint32_t; // also the index in the list, and the CLAP param id
	using ValueMap = std::map<std::string, double>; // param values by name

	enum class Group
	{
		Main,  // plugin params (fixed)
		Shader // params reflected from the current shader (replaced on every shader change)
	};

	// A numeric parameter, value normalized to [0, 1]
	struct Param
	{
		Id id = 0;
		std::string name;
		Group group = Group::Main;
		std::string units;
		double defaultValue = 0.5;
		bool automatable = false; // exposed to the host (clap.params)
	};

	// Fixed params, always first in the list
	enum DefaultId : Id
	{
		AudioGain,
		VideoParam,

		DefaultCount
	};

	// The parameter list:
	// - metadata (Param) is guarded by a mutex
	// - values are lock-free, so the audio and video threads can read/write them without blocking
	// - values changed by the web UI are flagged, to be forwarded to the host from the audio thread
	class ParamList
	{
	  public:
		static constexpr size_t maxCount = 256;

		ParamList();

		// -------- any thread, lock-free --------

		double value(Id id) const;
		void setValue(Id id, double value);
		size_t count() const;

		void flagForHost(Id id);
		// returns one flagged param at a time, false when there are none left
		bool takeFlaggedForHost(Id& id, double& value);

		// -------- non-realtime threads --------

		std::vector<Param> list() const;
		std::optional<Param> find(Id id) const;
		std::optional<Param> automatableAt(uint32_t index) const;
		uint32_t automatableCount() const;

		// replaces the Shader group; values are restored by name from `savedValues`, else defaulted
		void replaceShaderParams(std::vector<Param> shaderParams, const ValueMap& savedValues);

		// [{ id, name, group, units, value, defaultValue }, ...]
		json toJson() const;
		// { "<name>": value, ... }
		json valuesToJson() const;
		// applies values by name to the existing params
		void valuesFromJson(const json& values);

	  private:
		mutable std::mutex mutex;
		std::vector<Param> params;

		std::array<std::atomic<double>, maxCount> values{};
		std::atomic<size_t> paramCount{ 0 };

		std::array<std::atomic<bool>, maxCount> flaggedForHost{};
		std::atomic<bool> anyFlaggedForHost{ false };
	};
} // namespace ReaShader::Parameters
