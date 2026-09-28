/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// CLAP plugin shell -- the thin, mandatory-plugin-API glue, playing the same role
// src/vst3/ played for VST3 (mypluginentry.cpp + mypluginprocessor.cpp + myplugincontroller.cpp).
//
// Phase B status: owns a single ReaShaderPlugin instance (see reashaderplugin.h) and implements
// the CLAP-facing surface (audio-ports, params, state, gui) against it. Real Vulkan rendering is
// still NOT wired in -- process_frame below renders a REAPER project_time-driven diagnostic
// pattern (color cycling + a sweeping bar) instead of calling into a renderer; that's a separate,
// later pass.
//
// REAPER video tap access path (confirmed working -- see spikes/clap-video-tap, now retired):
//   host->get_extension(host, "cockos.reaper_extension")   -> reaper_plugin_info_t*
//   reaperInfo->GetFunc("clap_get_reaper_context")          -> clap_get_reaper_context()
//   clap_get_reaper_context(host, 4 /* sel=4: "FxDsp" */)   -> the fxctx video_CreateVideoProcessor wants
//   reaperInfo->GetFunc("video_CreateVideoProcessor")       -> video_CreateVideoProcessor()
//   video_CreateVideoProcessor(fxctx, VERSION)              -> IREAPERVideoProcessor*
// (now implemented in ReaShaderPlugin::activate(), src/reashader/reashaderplugin.cpp)

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <clap/clap.h>

#include "wdltypes.h" // video_frame.h needs WDL_FIXALIGN/INT_PTR but doesn't include this itself
#include "video_frame.h"

#include "plugin_state.h"

namespace ReaShader
{
	namespace
	{
		constexpr const char* kPluginId = "com.emanuelemessina.reashader";
		constexpr const char* kPluginName = "ReaShader";

		// REAPER's 'RGBA' video fourcc is packed in memory as B,G,R,A (byte0 = B) -- confirmed
		// empirically during the spike (a literal intended as opaque red rendered as opaque blue).
		// Build pixels through this helper instead of hex literals.
		inline int makePixelBGRA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
		{
			return (int)(((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
		}

		// -------- REAPER video processor callbacks --------

		// Phase A placeholder, still in place: paints a project_time-driven diagnostic pattern so
		// the pipeline stays verifiable end-to-end. A later pass replaces this with
		// ReaShaderRenderer::drawFrame() et al.
		IVideoFrame* processFrame(IREAPERVideoProcessor* vproc, const double* /*parmlist*/, int /*nparms*/,
								   double project_time, double /*frate*/, int /*force_format*/)
		{
			IVideoFrame* vf = vproc->newVideoFrame(640, 360, 'RGBA');
			if (vf)
			{
				int* bits = reinterpret_cast<int*>(vf->get_bits()); // get_bits() returns char* upstream
				int w = vf->get_w();
				int h = vf->get_h();
				int rowspanInts = vf->get_rowspan() / (int)sizeof(int);

				int phase = (int)std::fmod(project_time, 6.0) / 2; // 0, 1, or 2
				int bg = makePixelBGRA(phase == 0 ? 255 : 0, phase == 1 ? 255 : 0, phase == 2 ? 255 : 0, 255);
				int barColor = makePixelBGRA(255, 255, 255, 255);

				double sweep = project_time - std::floor(project_time); // 0..1 over 1 second
				int barX = (int)(sweep * w);
				const int barWidth = 20;

				for (int y = 0; y < h; y++)
				{
					int* row = bits + y * rowspanInts;
					for (int x = 0; x < w; x++)
						row[x] = (x >= barX && x < barX + barWidth) ? barColor : bg;
				}
			}
			return vf;
		}

		bool getVideoParamLocal(IREAPERVideoProcessor* vproc, int idx, double* valueOut)
		{
			// ReaShaderPlugin::activate() binds m_videoproc->userdata to `this` directly
			auto* reaShaderPlugin = static_cast<ReaShaderPlugin*>(vproc->userdata);
			if (!reaShaderPlugin)
				return false;
			return reaShaderPlugin->getVideoTapParamValue(idx, valueOut);
		}

		// -------- clap_plugin_t vtable --------

		bool plugin_init(const clap_plugin_t* plugin)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			state->plugin = std::make_unique<ReaShaderPlugin>();
			state->plugin->initialize();
			return true;
		}

		void plugin_destroy(const clap_plugin_t* plugin)
		{
			delete static_cast<ClapPluginState*>(plugin->plugin_data);
			delete plugin;
		}

		bool plugin_activate(const clap_plugin_t* plugin, double /*sample_rate*/, uint32_t /*min_frames*/,
							  uint32_t /*max_frames*/)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			state->plugin->activate(state->host);
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

		// scans incoming param-value events (host automation / generic host UI) and applies them;
		// drains outgoing param-value notifications (web UI edits) into out_events. Shared by
		// process() and flush() -- CLAP explicitly allows flush() to be used for the "not
		// currently processing audio" case with the same event-queue shape.
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
				while (state->plugin->popPendingHostNotification(id, value))
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

			// audio path is a simple gain passthrough -- real DSP is video, not audio; this just
			// keeps the plugin a well-formed track FX and exercises the Audio Gain param end to end
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

		void plugin_on_main_thread(const clap_plugin_t*) {}

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
			return state->plugin->automatableParamCount();
		}

		bool params_get_info(const clap_plugin_t* plugin, uint32_t index, clap_param_info_t* info)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			return state->plugin->getAutomatableParamInfo(index, info);
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
			state->plugin->saveState(stream);
			return true;
		}

		bool state_load(const clap_plugin_t* plugin, const clap_istream_t* stream)
		{
			auto* state = static_cast<ClapPluginState*>(plugin->plugin_data);
			state->plugin->loadState(stream);
			return true;
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
				return &reashaderClapGuiExtension;
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
															 "0.0.1",
															 "THE Video Processor for Reaper (CLAP port, rendering not yet ported)",
															 nullptr };

		const clap_plugin_t* create_plugin(const clap_plugin_factory_t*, const clap_host_t* host, const char* pluginId)
		{
			if (std::strcmp(pluginId, kPluginId) != 0)
				return nullptr;

			auto* state = new ClapPluginState();
			state->host = host;

			auto* plugin = new clap_plugin_t();
			plugin->desc = &pluginDescriptor;
			plugin->plugin_data = state;
			plugin->init = plugin_init;
			plugin->destroy = plugin_destroy;
			plugin->activate = plugin_activate;
			plugin->deactivate = plugin_deactivate;
			plugin->start_processing = plugin_start_processing;
			plugin->stop_processing = plugin_stop_processing;
			plugin->reset = plugin_reset;
			plugin->process = plugin_process;
			plugin->get_extension = plugin_get_extension;
			plugin->on_main_thread = plugin_on_main_thread;
			return plugin;
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

	// Referenced from reashaderplugin.cpp (via reashaderplugin.h's declaration, in a different
	// translation unit) as the REAPER video-processor callbacks -- must have external linkage,
	// hence defined here outside the anonymous namespace with names matching the header exactly.
	// Kept as free functions since they're CLAP/REAPER-video-tap glue, not ReaShaderPlugin's own
	// domain logic (they'll move once the renderer is ported).
	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms,
									double project_time, double frate, int force_format)
	{
		return processFrame(vproc, parmlist, nparms, project_time, frate, force_format);
	}
	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut)
	{
		return getVideoParamLocal(vproc, idx, valueOut);
	}
} // namespace ReaShader

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, ReaShader::entry_init,
																  ReaShader::entry_deinit,
																  ReaShader::entry_get_factory };
