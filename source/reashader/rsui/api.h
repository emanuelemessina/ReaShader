/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "rsparams/rsparams.h"
#include "tools/logging.h"
#include "tools/strings.h"
#include "vkt/vktcommon.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

#include <unordered_map>

namespace ReaShader
{
	struct TrackInfo
	{
		int number;
		const char* name;
	};

	namespace RSUI
	{
		enum MessageType
		{
			VSTParamUpdate,
			ParamUpdate,
			TrackInfo,
			Request,
			ParamGroupsList,
			ParamsList,
			ServerShutdown,
			RenderingDevicesList,
			RenderingDeviceChange,
			ParamAdd,
			ParamTypesList,

			numMessageTypes
		};

		// respect the messagetype enum order and size
		static const std::string typeStrings[] = { 
			"vstParamUpdate",
			"paramUpdate",
												   "trackInfo",
												   "request",
												   "paramGroupsList",
												   "paramsList",
												   "serverShutdown",
												   "renderingDevicesList",
												   "renderingDeviceChange",
												   "paramAdd",
												   "paramTypesList"
		};

		/**
		 Incoming requests from frontend
		 */
		enum RequestType
		{
			WantTrackInfo,
			WantParamGroupsList,
			WantParamsList,
			WantRenderingDevicesList,
			WantParamTypesList,

			numRequestTypes
		};

		static const std::unordered_map<std::string, RequestType> requestTypeMap = {
			{ typeStrings[TrackInfo], WantTrackInfo },
			{ typeStrings[ParamGroupsList], WantParamGroupsList },
			{ typeStrings[ParamsList], WantParamsList },
			{ typeStrings[RenderingDevicesList], WantRenderingDevicesList },
			{ typeStrings[ParamTypesList], WantParamTypesList }
		};

		//--------------------------------------

		class MessageHandler
		{
		  public:
			MessageHandler(json msg) : msg(msg)
			{
				_checkMsg();
			}
			/**
			 * @brief Performs json parsing
			 */
			MessageHandler(const char* json)
			{
				try
				{
					msg = json::parse(json);
					_checkMsg();
				}
				catch (STDEXC e)
				{
					LOG(WARNING, toConsole | toFile, "RSUI Message Handler", "Cannot parse JSON message",
						std::format("Received: {} | Error: {}", json, e.what()));

					error = true;
				}
			}

			// vst params have a preferential path because they can be updated also from the processor
			// other param types take the more general route
			MessageHandler& reactToVSTParamUpdate(
				const std::function<void(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue newValue)>& callback)
			{
				_reactTo(MessageType::VSTParamUpdate, [&](const json& msg) {
					callback(msg["paramId"], msg["value"]);
				});

				return *this;
			}

			MessageHandler& reactToParamUpdate(
				const std::function<void(Steinberg::Vst::ParamID, json newValue)>& callback)
			{
				_reactTo(MessageType::ParamUpdate, [&](const json& msg) { callback(msg["paramId"], msg["value"]); });

				return *this;
			}

			MessageHandler& reactToParamAdd(
				const std::function<void(std::unique_ptr<Parameters::IParameter>)>&
					callback)
			{
				Parameters::Type paramType = (Parameters::Type)msg["param"]["typeId"];

				if (paramType >= Parameters::Type::numParamTypes)
				{
					error = true;
					return *this;
				}

				Parameters::TypeInstantiator ti{};
				std::unique_ptr<Parameters::IParameter> newParam = ti.wield(paramType);
				if (newParam == nullptr) // although it shouldn't because the previous check should've failed
				{
					error = true;
					return *this;
				}
				newParam->fromJson(msg["param"]);

				_reactTo(MessageType::ParamAdd,
						 [&](const json& msg) { callback(std::move(newParam)); });

				return *this;
			}

			MessageHandler& reactToRenderingDeviceChange(const std::function<void(uint32_t)>& callback)
			{
				_reactTo(MessageType::RenderingDeviceChange, [&](const json& msg) { callback(msg["id"]); });

				return *this;
			}

			MessageHandler& reactToRequest(const std::function<void(RequestType)>& callback)
			{
				if (!(_hasField("what")))
					return *this;

				std::string what(msg["what"]);

				if (requestTypeMap.find(what) != requestTypeMap.end())
				{
					RequestType requestType = requestTypeMap.at(what);
					_reactTo(MessageType::Request, [&](const json& msg) { callback(requestType); });
				}

				return *this;
			}

			/**
			 * @brief Called if the handler does not react to any specified type
			 * @param callback
			 */
			void fallback(const std::function<void(const json&)>& callback)
			{
				if (error)
					return;

				if (!reacted)
				{
					callback(msg);
				}
			}
			/**
			 * @brief Logs a warning if the handler does not react to any specified type
			 * @param sender
			 */
			void fallbackWarning(const char* sender)
			{
				if (error)
					return;

				if (!reacted)
				{
					LOG(WARNING, toConsole | toFile, sender, "Unexpectd JSON from web UI",
						std::format("Received: {}", msg.dump()));
				}
			}

		  private:
			json msg;
			bool error{ false };
			bool reacted{ false };
			void _checkMsg()
			{
				if (!(msg.contains("type") && msg["type"].is_string()))
				{
					LOG(WARNING, toConsole | toFile, "RSUI Message Handler", "Unrecognized JSON message",
						std::format("Got {}", msg.dump()));

					error = true;
				}
			}
			bool _hasField(const char* field)
			{
				return msg.contains(field);
			}
			void _reactTo(MessageType type, const std::function<void(const json&)>& callback)
			{
				if (error)
					return;

				if (msg["type"] == typeStrings[type])
				{
					reacted = true;
					callback(msg);
				}
			}
		};

		class MessageBuilder
		{
		  public:
			static json buildParamUpdate(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue newValue)
			{
				json j;
				j["type"] = typeStrings[ParamUpdate];
				j["paramId"] = id;
				j["value"] = newValue;
				return j;
			}
			static json buildTrackInfo(ReaShader::TrackInfo trackInfo)
			{
				json j;

				j["type"] = typeStrings[RSUI::TrackInfo];

				j["trackNumber"] = trackInfo.number;

				if (trackInfo.number == -1) // master track
					j["trackName"] = "MASTER";
				else if (trackInfo.number == 0)
					j["trackName"] = "Track Not Found";

				if (trackInfo.name)
				{
					j["trackName"] = trackInfo.name;
				}

				return j;
			}
			static json buildParamGroupsList()
			{
				json j;
				j["type"] = typeStrings[RSUI::ParamGroupsList];

				for (int i = 0; i < (int)ReaShader::Parameters::Group::numParamGroups; i++)
				{
					json groupProps;
					groupProps["name"] = ReaShader::Parameters::paramGroupStrings[i];

					j["groups"][i] = groupProps;
				}

				return j;
			}
			static json buildParamTypesList()
			{
				json j;
				j["type"] = typeStrings[RSUI::ParamTypesList];

				for (int i = 0; i < (int)ReaShader::Parameters::Type::numParamTypes; i++)
				{
					json typeProps;
					typeProps["name"] = ReaShader::Parameters::paramTypeStrings[i];

					j["types"][i] = typeProps;
				}

				return j;
			}
			static json buildParamsList(std::vector<std::unique_ptr<Parameters::IParameter>>& rsParams)
			{
				json j;
				j["type"] = typeStrings[RSUI::ParamsList];

				for (int i = 0; i < rsParams.size(); i++)
				{
					json paramProps = rsParams[i]->toJson();

					j["params"][rsParams[i]->id] = paramProps;
				}

				return j;
			}
			static json buildServerShutdown()
			{
				json j;
				j["type"] = typeStrings[ServerShutdown];
				return j;
			}
			static json buildRenderingDevicesList(int currentSelectedDevice, std::vector<VkPhysicalDeviceProperties>& suitableDevicesProperties)
			{
				json j;
				j["type"] = typeStrings[RSUI::RenderingDevicesList];

				j["selected"] = currentSelectedDevice;

				for (size_t i = 0; i < suitableDevicesProperties.size(); i++)
				{
					json deviceProps;
					deviceProps["name"] = suitableDevicesProperties[i].deviceName;

					j["devices"][i] = deviceProps;
				}
				return j;
			}
		};
	} // namespace RSUI
} // namespace ReaShader
