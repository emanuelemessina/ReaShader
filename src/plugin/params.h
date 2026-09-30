/**
 * @file
 * @brief The parameter list: the plugin's own params and the chain nodes', lock-free values.
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
	using Id = uint32_t;							// the CLAP param id: stable, not the param's index in the list
	using ValueMap = std::map<std::string, double>; // param values by name

	// Param ids:
	// - the plugin's own params have fixed ids (AudioGain)
	// - a chain node's params get 1 + node uid * kNodeSlots + slot, so their ids don't change when the list
	//   changes around them (nodes added, removed or moved)
	constexpr uint32_t kMaxNodes = 16;
	constexpr uint32_t kNodeSlots = 64; // params per node
	constexpr Id nodeParamId(uint32_t node, uint32_t slot)
	{
		return 1 + node * kNodeSlots + slot;
	}
	constexpr Id kMaxIds = nodeParamId(kMaxNodes, 0);

	// Fixed params' ids
	constexpr Id AudioGain = 0;

	// Fixed params' indices: always first in the list, in this order
	enum DefaultIndex : uint32_t
	{
		AudioGainIndex,

		DefaultCount
	};

	enum class Group
	{
		Main, // plugin params (fixed), host-only
		Node  // a chain node's params (replaced on every change to the chain's nodes)
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
		bool automatable = false;	  // exposed to the host (clap.params)
		std::optional<uint32_t> node; // the chain node's uid (group Node)
	};

	// A chain node's params, in slot order
	struct NodeParams
	{
		uint32_t node = 0;
		std::vector<Param> params;
	};

	// The parameter list:
	// - metadata (Param) is guarded by a mutex
	// - values are lock-free, by id, so the audio and video threads can read/write them without blocking
	// - the list order (index) is the CLAP param order and REAPER's parmlist order
	// - values changed by the web UI are flagged, to be forwarded to the host from the audio thread
	class ParamList
	{
	  public:
		static constexpr size_t maxCount = 256;

		ParamList();

		// -------- any thread, lock-free --------

		double value(Id id) const;
		void setValue(Id id, double value);
		bool contains(Id id) const;
		size_t count() const;
		// the value of the param at `index` in the list (0 past the end)
		double valueAt(size_t index) const;

		void flagForHost(Id id);
		// returns one flagged param at a time, in list order, false when there are none left
		bool takeFlaggedForHost(Id& id, double& value);

		// -------- non-realtime threads --------

		std::vector<Param> list() const;
		std::optional<Param> find(Id id) const;
		std::optional<Param> automatableAt(uint32_t index) const;
		uint32_t automatableCount() const;

		// Replaces the Node group with `nodes`' params, in order, at ids nodeParamId(node, slot) (at most kNodeSlots
		// per node, maxCount in all). Each value comes from `savedValues` by name, else from the param that had
		// the same id and name (kept through a reorder, within the new range), else from its default.
		void replaceNodeParams(std::vector<NodeParams> nodes, const ValueMap& savedValues);

		// [{ id, name, label, group, node, units, value, defaultValue, minValue, maxValue }, ...]
		json toJson() const;
		// { "<name>": value, ... }
		json valuesToJson() const;
		// applies values by name to the existing params
		void valuesFromJson(const json& values);

	  private:
		// rebuilds idAt and indexOfId from params (mutex held)
		void _index();

		mutable std::mutex mutex;
		std::vector<Param> params;

		std::array<std::atomic<double>, kMaxIds> values{};
		std::array<std::atomic<bool>, kMaxIds> flaggedForHost{};
		std::atomic<bool> anyFlaggedForHost{ false };

		std::array<std::atomic<Id>, maxCount> idAt{};		 // index -> id
		std::array<std::atomic<int32_t>, kMaxIds> indexOfId; // id -> index, -1 = no such param
		std::atomic<size_t> paramCount{ 0 };
	};
} // namespace ReaShader::Parameters
