/**
 * @file
 * @brief The parameter list: fixed and shader params, lock-free values.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

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
		Main,  // plugin params (fixed), host-only
		Lut,   // the LUT's params (fixed), shown with the LUT in the web UI
		Shader // params reflected from the current shader (replaced on every shader change)
	};

	// A numeric parameter
	struct Param
	{
		Id id = 0;
		std::string name;  // identifies it in saved state
		std::string label; // shown in the UI and the host
		Group group = Group::Main;
		std::string units;
		double defaultValue = 0.5;
		double minValue = 0.0;
		double maxValue = 1.0;
		bool automatable = false; // exposed to the host (clap.params)
	};

	// Fixed params, always first in the list
	enum DefaultId : Id
	{
		AudioGain,
		LutMix, // 0 = the frame as is, 1 = fully through the LUT

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

		// [{ id, name, label, group, units, value, defaultValue, minValue, maxValue }, ...]
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
