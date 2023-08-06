#pragma once

#include "rsparams.h"
#include "tools/logging.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

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
			ParamUpdate,
			TrackInfo,
			Request,
			ParamsList,

			numMessageTypes
		};

		static const std::string typeStrings[] = { "paramUpdate", "trackInfo", "request", "paramsList" };

		enum RequestType
		{
			WantTrackInfo,
			WantParamsList,

			numRequestTypes
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

			MessageHandler& reacToParamUpdate(
				const std::function<void(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue)>& callback)
			{
				_reactTo(MessageType::ParamUpdate, [&](const json& msg) { callback(msg["paramId"], msg["value"]); });

				return *this;
			}

			MessageHandler& reacToTrackInfo(const std::function<void(const ReaShader::TrackInfo)>& callback)
			{
				_reactTo(MessageType::TrackInfo, [&](const json& msg) {
					callback(ReaShader::TrackInfo(msg["trackNumber"], ((std::string)msg["trackName"]).c_str()));
				});

				return *this;
			}

			MessageHandler& reactToRequest(const std::function<void(RequestType)>& callback)
			{
				std::string what(msg["what"]);
				for (size_t i = 0; i < numMessageTypes; ++i)
				{
					if (typeStrings[i] == what)
					{
						_reactTo(MessageType::Request, [&](const json& msg) { callback(static_cast<RequestType>(i)); });
						break;
					}
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
			static json buildParametersList(ReaShaderParameter (&rsParams)[uNumParams])
			{
				json j;
				j["type"] = typeStrings[RSUI::ParamsList];

				for (int i; i < uNumParams; i++)
				{
					json paramProps;
					paramProps["title"] = rsParams[i].title;
					paramProps["units"] = rsParams[i].units;
					paramProps["defaultValue"] = rsParams[i].defaultValue;
					paramProps["value"] = rsParams[i].value;

					j["params"][rsParams->id] = paramProps;
				}

				return j;
			}
		};
	} // namespace RSUI
} // namespace ReaShader
