/**
 * @file
 * @brief CLAP entry point: the plugin factory, the descriptor, and the C callbacks forwarding to ReaShaderPlugin.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <clap/clap.h>

#include "clap/plugin_state.h"
#include "util/logging.h"

namespace ReaShader
{
	namespace
	{
		// set by CMakeLists.txt: debug builds get their own name and id
		constexpr const char* kPluginId = REASHADER_ID;
		constexpr const char* kPluginName = REASHADER_NAME;

		// -------- clap_plugin_t vtable --------

		bool plugin_init(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);

			// message boxes are shown from on_main_thread (see logging.h)
			const clap_host_t* host = state->host;
			registerBoxRequester(state, [host]() { host->request_callback(host); });

			state->plugin = std::make_unique<ReaShaderPlugin>();
			state->plugin->initialize(state->host);
			return true;
		}

		void plugin_destroy(const clap_plugin_t* plugin)
		{
			unregisterBoxRequester(plugin->plugin_data);
			delete static_cast<ClapPluginState*>(plugin->plugin_data);
		}

		bool plugin_activate(const clap_plugin_t* plugin, double /*sample_rate*/, uint32_t /*min_frames*/,
							  uint32_t /*max_frames*/)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			state->plugin->activate();
			return true;
		}

		void plugin_deactivate(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			state->plugin->deactivate();
		}

		bool plugin_start_processing(const clap_plugin_t*)
		{
			return true;
		}
		void plugin_stop_processing(const clap_plugin_t*) {}
		void plugin_reset(const clap_plugin_t*) {}

		// Applies incoming param changes (host automation, host UI) and pushes out the changes made
		// in the web UI. Shared by process() and flush(); the host calls flush() when not processing.
		void handleParamEvents(ClapPluginState* state, const clap_input_events_t* in,
								const clap_output_events_t* out)
		{
			if (in)
			{
				uint32_t n = in->size(in);
				for (uint32_t i = 0; i < n; i++)
				{
					const clap_event_header_t* hdr = in->get(in, i);
					if (hdr->space_id == CLAP_CORE_EVENT_SPACE_ID && hdr->type == CLAP_EVENT_PARAM_VALUE)
					{
						auto* ev = reinterpret_cast<const clap_event_param_value_t*>(hdr);
						state->plugin->applyHostParamValue(ev->param_id, ev->value);
					}
				}
			}

			if (out)
			{
				clap_id id;
				double value;
				while (state->plugin->takeParamChangeForHost(id, value))
				{
					clap_event_param_value_t ev{};
					ev.header.size = sizeof(ev);
					ev.header.time = 0;
					ev.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
					ev.header.type = CLAP_EVENT_PARAM_VALUE;
					ev.header.flags = 0;
					ev.param_id = id;
					ev.cookie = nullptr;
					ev.note_id = -1;
					ev.port_index = -1;
					ev.channel = -1;
					ev.key = -1;
					ev.value = value;
					out->try_push(out, &ev.header);
				}
			}
		}

		clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);

			handleParamEvents(state, process->in_events, process->out_events);

			// audio is a plain gain passthrough: the real work is on video, this keeps it a normal track FX
			if (process->audio_outputs_count > 0)
			{
				clap_audio_buffer_t& out = process->audio_outputs[0];
				bool haveInput = process->audio_inputs_count > 0;
				float gain = (float)state->plugin->getAudioGain();

				for (uint32_t ch = 0; ch < out.channel_count; ch++)
				{
					if (haveInput && ch < process->audio_inputs[0].channel_count && out.data32 &&
						process->audio_inputs[0].data32)
					{
						const float* in = process->audio_inputs[0].data32[ch];
						float* o = out.data32[ch];
						for (uint32_t s = 0; s < process->frames_count; s++)
							o[s] = in[s] * gain;
					}
					else if (out.data32)
					{
						std::memset(out.data32[ch], 0, sizeof(float) * process->frames_count);
					}
				}
			}
			return CLAP_PROCESS_CONTINUE;
		}

		void plugin_on_main_thread(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			state->plugin->onMainThread();
			showQueuedBoxes();
		}

		// -------- clap.audio-ports (one stereo in/out, so REAPER treats this as a normal FX) --------

		uint32_t audio_ports_count(const clap_plugin_t*, bool /*is_input*/)
		{
			return 1;
		}

		bool audio_ports_get(const clap_plugin_t*, uint32_t index, bool is_input, clap_audio_port_info_t* info)
		{
			if (index != 0)
				return false;

			info->id = 0;
			std::snprintf(info->name, sizeof(info->name), "%s", is_input ? "Stereo In" : "Stereo Out");
			info->flags = CLAP_AUDIO_PORT_IS_MAIN;
			info->channel_count = 2;
			info->port_type = CLAP_PORT_STEREO;
			info->in_place_pair = CLAP_INVALID_ID;
			return true;
		}

		const clap_plugin_audio_ports_t audioPortsExtension = { audio_ports_count, audio_ports_get };

		// -------- clap.params --------

		uint32_t params_count(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->paramCount();
		}

		bool params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->getParamInfo(index, info);
		}

		bool params_get_value(const clap_plugin_t* plugin, clap_id id, double* value)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->getParamValue(id, value);
		}

		bool params_value_to_text(const clap_plugin_t* plugin, clap_id id, double value, char* buf, uint32_t size)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->valueToText(id, value, buf, size);
		}

		bool params_text_to_value(const clap_plugin_t* plugin, clap_id id, const char* text, double* value)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->textToValue(id, text, value);
		}

		void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in, const clap_output_events_t* out)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			handleParamEvents(state, in, out);
		}

		const clap_plugin_params_t paramsExtension = { params_count,        params_get_info,   params_get_value,
														params_value_to_text, params_text_to_value, params_flush };

		// -------- clap.state --------

		bool state_save(const clap_plugin_t* plugin, const clap_ostream_t* stream)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->saveState(stream);
		}

		bool state_load(const clap_plugin_t* plugin, const clap_istream_t* stream)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->loadState(stream);
		}

		const clap_plugin_state_t stateExtension = { state_save, state_load };

		// -------- extension dispatch --------

		const void* plugin_get_extension(const clap_plugin_t*, const char* id)
		{
			if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
				return &audioPortsExtension;
			if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
				return &paramsExtension;
			if (std::strcmp(id, CLAP_EXT_STATE) == 0)
				return &stateExtension;
#ifdef _WIN32
			if (std::strcmp(id, CLAP_EXT_GUI) == 0)
				return &guiExtension;
#endif
			return nullptr;
		}

		// -------- factory --------

		const clap_plugin_descriptor_t pluginDescriptor = { CLAP_VERSION_INIT,
															 kPluginId,
															 kPluginName,
															 "Emanuele Messina",
															 "https://github.com/emanuelemessina/ReaShader",
															 "",
															 "",
															 REASHADER_VERSION,
															 "THE Video Processor for Reaper",
															 nullptr };

		const clap_plugin_t* create_plugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
		{
			if (std::strcmp(pluginId, kPluginId) != 0)
				return nullptr;

			// deleted by plugin_destroy
			auto* state = new ClapPluginState();
			state->host = host;
			state->clapPlugin = { &pluginDescriptor, state,
								  plugin_init, plugin_destroy,
								  plugin_activate, plugin_deactivate,
								  plugin_start_processing, plugin_stop_processing,
								  plugin_reset, plugin_process,
								  plugin_get_extension, plugin_on_main_thread };
			return &state->clapPlugin;
		}

		uint32_t get_plugin_count(const clap_plugin_factory_t*)
		{
			return 1;
		}

		const clap_plugin_descriptor_t* get_plugin_descriptor(const clap_plugin_factory_t*, uint32_t index)
		{
			return index == 0 ? &pluginDescriptor : nullptr;
		}

		const clap_plugin_factory_t pluginFactory = { get_plugin_count, get_plugin_descriptor, create_plugin };

		bool entry_init(const char*)
		{
			return true;
		}
		void entry_deinit() {}

		const void* entry_get_factory(const char* factoryId)
		{
			if (std::strcmp(factoryId, CLAP_PLUGIN_FACTORY_ID) == 0)
				return &pluginFactory;
			return nullptr;
		}
	} // namespace

} // namespace ReaShader

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, ReaShader::entry_init,
																  ReaShader::entry_deinit,
																  ReaShader::entry_get_factory };
