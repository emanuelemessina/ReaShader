/**
 * @file
 * @brief The parameter list: fixed and shader params, lock-free values.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "params.h"

#include "util/logging.h"

#include <nlohmann/json.hpp>

#include <format>

namespace ReaShader::Parameters
{
	namespace
	{
		const char* groupName(Group group)
		{
			return group == Group::Main ? "main" : "shader";
		}
	} // namespace

	ParamList::ParamList()
	{
		params = {
			{ AudioGain, "Audio Gain", "Audio Gain", Group::Main, "%", 1.0, 0.0, 1.0, true }, // 1.0 = unchanged audio
		};
		for (const Param& p : params)
			values[p.id] = p.defaultValue;
		paramCount = params.size();
	}

	// -------- any thread, lock-free --------

	double ParamList::value(Id id) const
	{
		return id < maxCount ? values[id].load() : 0.0;
	}

	void ParamList::setValue(Id id, double value)
	{
		if (id < maxCount)
			values[id] = value;
	}

	size_t ParamList::count() const
	{
		return paramCount;
	}

	void ParamList::flagForHost(Id id)
	{
		if (id >= maxCount)
			return;
		flaggedForHost[id] = true;
		anyFlaggedForHost = true;
	}

	bool ParamList::takeFlaggedForHost(Id& id, double& value)
	{
		// cleared before the scan: a flag set meanwhile sets it again, so it's never left unseen
		if (!anyFlaggedForHost.exchange(false))
			return false;

		for (Id i = 0; i < paramCount; i++)
		{
			if (flaggedForHost[i].exchange(false))
			{
				anyFlaggedForHost = true; // there may be more
				id = i;
				value = values[i];
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
		if (id >= params.size())
			return std::nullopt;
		return params[id];
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

	void ParamList::replaceShaderParams(std::vector<Param> shaderParams, const ValueMap& savedValues)
	{
		std::lock_guard lock(mutex);

		params.resize(DefaultCount);
		for (Param& p : shaderParams)
		{
			if (params.size() >= maxCount)
			{
				LOG(WARNING, toConsole | toFile, "Params", "Too many shader params",
					std::format("Only the first {} are kept", maxCount - DefaultCount));
				break;
			}
			p.id = (Id)params.size();
			p.group = Group::Shader;
			auto saved = savedValues.find(p.name);
			values[p.id] = saved != savedValues.end() ? saved->second : p.defaultValue;
			params.push_back(std::move(p));
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
