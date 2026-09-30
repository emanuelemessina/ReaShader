/**
 * @file
 * @brief The parameter list: the plugin's own params and the chain nodes', lock-free values.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "params.h"

#include "util/logging.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>

namespace ReaShader::Parameters
{
	namespace
	{
		const char* groupName(Group group)
		{
			return group == Group::Main ? "main" : "node";
		}
	} // namespace

	ParamList::ParamList()
	{
		params = {
			// 1.0 = unchanged audio
			{ AudioGain, "Audio Gain", "Audio Gain", Group::Main, "%", 1.0, 0.0, 1.0, true, {} },
		};
		for (const Param& p : params)
			values[p.id] = p.defaultValue;
		_index();
	}

	// -------- any thread, lock-free --------

	double ParamList::value(Id id) const
	{
		return id < kMaxIds ? values[id].load() : 0.0;
	}

	void ParamList::setValue(Id id, double value)
	{
		if (id < kMaxIds)
			values[id] = value;
	}

	bool ParamList::contains(Id id) const
	{
		return id < kMaxIds && indexOfId[id] >= 0;
	}

	size_t ParamList::count() const
	{
		return paramCount;
	}

	double ParamList::valueAt(size_t index) const
	{
		return index < paramCount ? values[idAt[index]].load() : 0.0;
	}

	void ParamList::flagForHost(Id id)
	{
		if (!contains(id))
			return;
		flaggedForHost[id] = true;
		anyFlaggedForHost = true;
	}

	bool ParamList::takeFlaggedForHost(Id& id, double& value)
	{
		// cleared before the scan: a flag set meanwhile sets it again, so it's never left unseen
		if (!anyFlaggedForHost.exchange(false))
			return false;

		for (size_t i = 0; i < paramCount; i++)
		{
			Id flagged = idAt[i];
			if (flaggedForHost[flagged].exchange(false))
			{
				anyFlaggedForHost = true; // there may be more
				id = flagged;
				value = values[flagged];
				return true;
			}
		}
		return false;
	}

	// -------- non-realtime threads --------

	std::vector<Param> ParamList::list() const
	{
		std::lock_guard lock(mutex);
		return params;
	}

	std::optional<Param> ParamList::find(Id id) const
	{
		std::lock_guard lock(mutex);
		if (!contains(id))
			return std::nullopt;
		return params[(size_t)indexOfId[id]];
	}

	std::optional<Param> ParamList::automatableAt(uint32_t index) const
	{
		std::lock_guard lock(mutex);
		for (const Param& p : params)
		{
			if (!p.automatable)
				continue;
			if (index == 0)
				return p;
			index--;
		}
		return std::nullopt;
	}

	uint32_t ParamList::automatableCount() const
	{
		std::lock_guard lock(mutex);
		uint32_t count = 0;
		for (const Param& p : params)
			count += p.automatable ? 1 : 0;
		return count;
	}

	void ParamList::replaceNodeParams(std::vector<NodeParams> nodes, const ValueMap& savedValues)
	{
		std::lock_guard lock(mutex);

		std::vector<Param> previous(params.begin() + DefaultCount, params.end());
		params.resize(DefaultCount);

		bool dropped = false;
		for (NodeParams& node : nodes)
		{
			uint32_t slot = 0;
			for (Param& p : node.params)
			{
				if (slot >= kNodeSlots || params.size() >= maxCount)
				{
					dropped = true;
					break;
				}
				p.id = nodeParamId(node.node, slot++);
				p.group = Group::Node;
				p.node = node.node;

				double value = p.defaultValue;
				auto saved = savedValues.find(p.name);
				auto same = std::find_if(previous.begin(), previous.end(),
										 [&](const Param& old) { return old.id == p.id && old.name == p.name; });
				if (saved != savedValues.end())
					value = saved->second;
				else if (same != previous.end())
					value = std::clamp(values[p.id].load(), p.minValue, p.maxValue);
				values[p.id] = value;
				params.push_back(std::move(p));
			}
		}
		if (dropped)
			LOG(WARNING, toConsole | toFile, "Params", "Too many params",
				std::format("At most {} per node and {} in all are kept", kNodeSlots, maxCount));

		// ids that stop being params drop their pending flags
		for (const Param& old : previous)
		{
			bool kept = std::any_of(params.begin(), params.end(), [&](const Param& p) { return p.id == old.id; });
			if (!kept)
				flaggedForHost[old.id] = false;
		}
		_index();
	}

	void ParamList::_index()
	{
		for (auto& index : indexOfId)
			index = -1;
		for (size_t i = 0; i < params.size(); i++)
		{
			idAt[i] = params[i].id;
			indexOfId[params[i].id] = (int32_t)i;
		}
		paramCount = params.size();
	}

	json ParamList::toJson() const
	{
		std::lock_guard lock(mutex);
		json list = json::array();
		for (const Param& p : params)
		{
			list.push_back({ { "id", p.id },
							 { "name", p.name },
							 { "label", p.label },
							 { "group", groupName(p.group) },
							 { "node", p.node ? json(*p.node) : json() },
							 { "units", p.units },
							 { "value", values[p.id].load() },
							 { "defaultValue", p.defaultValue },
							 { "minValue", p.minValue },
							 { "maxValue", p.maxValue } });
		}
		return list;
	}

	json ParamList::valuesToJson() const
	{
		std::lock_guard lock(mutex);
		json result = json::object();
		for (const Param& p : params)
			result[p.name] = values[p.id].load();
		return result;
	}

	void ParamList::valuesFromJson(const json& savedValues)
	{
		std::lock_guard lock(mutex);
		for (const Param& p : params)
		{
			if (savedValues.contains(p.name) && savedValues[p.name].is_number())
				values[p.id] = savedValues[p.name].get<double>();
		}
	}
} // namespace ReaShader::Parameters
