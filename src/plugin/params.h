/**
 * @file
 * @brief The parameter list: the plugin's own params and the chain nodes' slots, lock-free values.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <algorithm>
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
	using Id = uint32_t;							// the CLAP param id, which is also its index in the list
	using ValueMap = std::map<std::string, double>; // param values by name

	// The list never changes size, so every id exists from the start (REAPER binds a project's envelopes by id
	// right after loading the state, while the plugin is active):
	// - the plugin's own params first (AudioGain);
	// - then kNodeSlots slots per chain node uid: a node's params are at 1 + uid * kNodeSlots + slot. Slots no
	//   node uses have an empty name, and the host hides them.
	constexpr uint32_t kMaxNodes = 16;
	constexpr uint32_t kNodeSlots = 40; // params per node: a shader with more sliders is rejected
	constexpr Id nodeParamId(uint32_t node, uint32_t slot)
	{
		return 1 + node * kNodeSlots + slot;
	}
	constexpr Id kParamCount = nodeParamId(kMaxNodes, 0);

	// Fixed params' ids
	constexpr Id AudioGain = 0;

	enum class Group
	{
		Main, // plugin params (fixed), host-only
		Node  // a chain node's slot (replaced on every change to the chain's nodes)
	};

	// A numeric parameter, or an unused node slot (no node, empty name and label)
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
		std::optional<uint32_t> node; // the chain node's uid (a used Node slot)

		bool used() const
		{
			return group == Group::Main || node.has_value();
		}

		// The host sees every param as 0..1 over its min..max: CLAP lets a param's range (and default) change
		// only with rescan(ALL), which isn't allowed while active, so the host keeps the range it first scanned.
		double toReal(double hostValue) const
		{
			return minValue + hostValue * (maxValue - minValue);
		}
		double toHost(double realValue) const
		{
			return maxValue > minValue ? std::clamp((realValue - minValue) / (maxValue - minValue), 0.0, 1.0) : 0.0;
		}
	};

	// A chain node's params, in slot order
	struct NodeParams
	{
		uint32_t node = 0;
		std::vector<Param> params;
	};

	// The parameter list:
	// - metadata (Param) is guarded by a mutex
	// - values are the host's (0..1, see Param::toHost), lock-free, by id, so the audio and video threads can
	//   read/write them without blocking
	// - values changed by the web UI are flagged, to be forwarded to the host from the audio thread
	class ParamList
	{
	  public:
		ParamList();

		// -------- any thread, lock-free --------

		double value(Id id) const; // the host value (0..1), 0 past the end
		void setValue(Id id, double value);
		bool used(Id id) const;

		void flagForHost(Id id); // used params only
		// returns one flagged param at a time, in id order, false when there are none left
		bool takeFlaggedForHost(Id& id, double& value);

		// -------- non-realtime threads --------

		double realValue(Id id) const;			// in the param's own range
		void setRealValue(Id id, double value); // in the param's own range, clamped to it
		Param at(Id id) const;					// any id, used or not (id < kParamCount)
		std::optional<Param> find(Id id) const; // a used param
		std::vector<Param> list() const;		// the used params, in id order

		// Replaces every node slot: `nodes`' params at nodeParamId(node, slot) (at most kNodeSlots per node), the
		// other slots unused. Each value comes from `savedValues` by name, else from the param that had the same
		// id and name (within the new range), else from its default.
		// Returns the ids of params that went away, or whose id now holds another param.
		std::vector<Id> replaceNodeParams(std::vector<NodeParams> nodes, const ValueMap& savedValues);

		// the used params, values in their own range:
		// [{ id, name, label, group, node, units, value, defaultValue, minValue, maxValue }, ...]
		json toJson() const;
		// the used params, values in their own range: { "<name>": value, ... }
		json valuesToJson() const;
		// applies values (in their own range) by name to the used params
		void valuesFromJson(const json& values);

	  private:
		mutable std::mutex mutex;
		std::vector<Param> params; // kParamCount, by id

		std::array<std::atomic<double>, kParamCount> values{};
		std::array<std::atomic<bool>, kParamCount> inUse{};
		std::array<std::atomic<bool>, kParamCount> flaggedForHost{};
		std::atomic<bool> anyFlaggedForHost{ false };
	};
} // namespace ReaShader::Parameters
