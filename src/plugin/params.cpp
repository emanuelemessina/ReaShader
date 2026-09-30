/**
 * @file
 * @brief The parameter list: the plugin's own params and the chain nodes' slots, lock-free values.
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

		Param unusedSlot(Id id)
		{
			Param slot;
			slot.id = id;
			slot.group = Group::Node;
			return slot;
		}
	} // namespace

	ParamList::ParamList()
	{
		params.reserve(kParamCount);
		// 1.0 = unchanged audio
		params.push_back({ AudioGain, "Audio Gain", "Audio Gain", Group::Main, "%", 1.0, 0.0, 1.0, {} });
		for (Id id = nodeParamId(0, 0); id < kParamCount; id++)
			params.push_back(unusedSlot(id));

		for (const Param& p : params)
		{
			values[p.id] = p.toHost(p.defaultValue);
			inUse[p.id] = p.used();
		}
	}

	// -------- any thread, lock-free --------

	double ParamList::value(Id id) const
	{
		return id < kParamCount ? values[id].load() : 0.0;
	}

	void ParamList::setValue(Id id, double value)
	{
		if (id < kParamCount)
			values[id] = value;
	}

	bool ParamList::used(Id id) const
	{
		return id < kParamCount && inUse[id];
	}

	void ParamList::flagForHost(Id id)
	{
		if (!used(id))
			return;
		flaggedForHost[id] = true;
		anyFlaggedForHost = true;
	}

	bool ParamList::takeFlaggedForHost(Id& id, double& value)
	{
		// cleared before the scan: a flag set meanwhile sets it again, so it's never left unseen
		if (!anyFlaggedForHost.exchange(false))
			return false;

		for (Id flagged = 0; flagged < kParamCount; flagged++)
		{
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

	double ParamList::realValue(Id id) const
	{
		std::lock_guard lock(mutex);
		return id < kParamCount ? params[id].toReal(values[id]) : 0.0;
	}

	void ParamList::setRealValue(Id id, double value)
	{
		std::lock_guard lock(mutex);
		if (id < kParamCount)
			values[id] = params[id].toHost(value);
	}

	Param ParamList::at(Id id) const
	{
		std::lock_guard lock(mutex);
		return id < kParamCount ? params[id] : unusedSlot(id);
	}

	std::optional<Param> ParamList::find(Id id) const
	{
		std::lock_guard lock(mutex);
		if (id >= kParamCount || !params[id].used())
			return std::nullopt;
		return params[id];
	}

	std::vector<Param> ParamList::list() const
	{
		std::lock_guard lock(mutex);
		std::vector<Param> result;
		for (const Param& p : params)
			if (p.used())
				result.push_back(p);
		return result;
	}

	std::vector<Id> ParamList::replaceNodeParams(std::vector<NodeParams> nodes, const ValueMap& savedValues)
	{
		std::lock_guard lock(mutex);

		std::vector<Param> previous = params;
		for (Id id = nodeParamId(0, 0); id < kParamCount; id++)
			params[id] = unusedSlot(id);

		bool dropped = false;
		for (NodeParams& node : nodes)
		{
			if (node.node >= kMaxNodes)
				continue;
			if (node.params.size() > kNodeSlots)
			{
				dropped = true;
				node.params.resize(kNodeSlots);
			}
			for (uint32_t slot = 0; slot < node.params.size(); slot++)
			{
				Param p = std::move(node.params[slot]);
				p.id = nodeParamId(node.node, slot);
				p.group = Group::Node;
				p.node = node.node;

				// in the param's own range: saved, else kept, else the default
				double value = p.defaultValue;
				auto saved = savedValues.find(p.name);
				if (saved != savedValues.end())
					value = saved->second;
				else if (previous[p.id].used() && previous[p.id].name == p.name)
					value = previous[p.id].toReal(values[p.id]);
				values[p.id] = p.toHost(value);
				params[p.id] = std::move(p);
			}
		}
		if (dropped)
			LOG(WARNING, toConsole | toFile, "Params", "Too many params",
				std::format("At most {} per node are kept", kNodeSlots));

		std::vector<Id> gone;
		for (Id id = nodeParamId(0, 0); id < kParamCount; id++)
		{
			inUse[id] = params[id].used();
			if (previous[id].used() && previous[id].name != params[id].name)
			{
				gone.push_back(id);
				flaggedForHost[id] = false;
			}
		}
		return gone;
	}

	json ParamList::toJson() const
	{
		std::lock_guard lock(mutex);
		json list = json::array();
		for (const Param& p : params)
		{
			if (!p.used())
				continue;
			list.push_back({ { "id", p.id },
							 { "name", p.name },
							 { "label", p.label },
							 { "group", groupName(p.group) },
							 { "node", p.node ? json(*p.node) : json() },
							 { "units", p.units },
							 { "value", p.toReal(values[p.id]) },
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
			if (p.used())
				result[p.name] = p.toReal(values[p.id]);
		return result;
	}

	void ParamList::valuesFromJson(const json& savedValues)
	{
		std::lock_guard lock(mutex);
		for (const Param& p : params)
		{
			if (p.used() && savedValues.contains(p.name) && savedValues[p.name].is_number())
				values[p.id] = p.toHost(savedValues[p.name].get<double>());
		}
	}
} // namespace ReaShader::Parameters
