/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

// CLAP plugin shell -- the thin, mandatory-plugin-API glue, playing the same role
// source/vst3/ played for VST3 (mypluginentry.cpp + mypluginprocessor.cpp + myplugincontroller.cpp).
//
// Phase A (build system migration) status: this is intentionally NOT wired to
// ReaShaderProcessor / ReaShaderRenderer / rsparams / rsui yet -- those still use
// Steinberg VST3 types (Vst::ParamID, Vst::ParamValue, IBStream, FUnknown, ...) pervasively
// and need a real port (Phase B). Until then, process_frame below renders a REAPER
// project_time-driven diagnostic pattern (color cycling + a sweeping bar) instead of calling
// into the real renderer, so the build pipeline itself can be verified end-to-end.
//
// REAPER video tap access path (confirmed working -- see spikes/clap-video-tap, now retired):
//   host->get_extension(host, "cockos.reaper_extension")   -> reaper_plugin_info_t*
//   reaperInfo->GetFunc("clap_get_reaper_context")          -> clap_get_reaper_context()
//   clap_get_reaper_context(host, 4 /* sel=4: "FxDsp" */)   -> the fxctx video_CreateVideoProcessor wants
//   reaperInfo->GetFunc("video_CreateVideoProcessor")       -> video_CreateVideoProcessor()
//   video_CreateVideoProcessor(fxctx, VERSION)              -> IREAPERVideoProcessor*
// This mirrors ReaShaderProcessor::activate() in source/reashader/rsprocessor.cpp, which uses
// IReaperHostApplication::getReaperParent(4) + getReaperApi("video_CreateVideoProcessor") instead.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <clap/clap.h>

#include "reaper_plugin.h"
#include "video_frame.h"
#include "video_processor.h"

namespace
{
	constexpr const char* kPluginId = "com.emanuelemessina.reashader";
	constexpr const char* kPluginName = "ReaShader";

	struct PluginState
	{
		const clap_host_t* host{ nullptr };
		IREAPERVideoProcessor* videoProcessor{ nullptr };
	};

	// REAPER's 'RGBA' video fourcc is packed in memory as B,G,R,A (byte0 = B) -- confirmed
	// empirically during the spike (a literal intended as opaque red rendered as opaque blue).
	// Build pixels through this helper instead of hex literals.
	inline int makePixelBGRA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
	{
		return (int)(((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
	}

	// -------- REAPER video processor callbacks --------

	// Phase A placeholder: paints a project_time-driven diagnostic pattern so the pipeline is
	// verifiable end-to-end. Phase B replaces this with ReaShaderRenderer::drawFrame() et al.
	IVideoFrame* processFrame(IREAPERVideoProcessor* vproc, const double* /*parmlist*/, int /*nparms*/,
							   double project_time, double /*frate*/, int /*force_format*/)
	{
		IVideoFrame* vf = vproc->newVideoFrame(640, 360, 'RGBA');
		if (vf)
		{
			int* bits = vf->get_bits();
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

	bool getVideoParam(IREAPERVideoProcessor* /*vproc*/, int /*idx*/, double* /*valueOut*/)
	{
		return false;
	}

	// -------- clap_plugin_t vtable --------

	bool plugin_init(const clap_plugin_t* /*plugin*/)
	{
		return true;
	}

	void plugin_destroy(const clap_plugin_t* plugin)
	{
		delete static_cast<PluginState*>(plugin->plugin_data);
		delete plugin;
	}

	bool plugin_activate(const clap_plugin_t* plugin, double /*sample_rate*/, uint32_t /*min_frames*/,
						  uint32_t /*max_frames*/)
	{
		auto* state = static_cast<PluginState*>(plugin->plugin_data);
		const clap_host_t* host = state->host;

		auto* reaperInfo = static_cast<reaper_plugin_info_t*>(
			const_cast<void*>(host->get_extension(host, "cockos.reaper_extension")));

		if (!reaperInfo || !reaperInfo->GetFunc)
			return true; // not hosted by REAPER (or extension unavailable) -- plugin still "works", just does nothing

		using ClapGetReaperContext = void* (*)(const clap_host_t*, int);
		using VideoCreateVideoProcessor = IREAPERVideoProcessor* (*)(void*, int);

		auto clapGetReaperContext = (ClapGetReaperContext)reaperInfo->GetFunc("clap_get_reaper_context");
		auto videoCreateVideoProcessor =
			(VideoCreateVideoProcessor)reaperInfo->GetFunc("video_CreateVideoProcessor");

		if (clapGetReaperContext && videoCreateVideoProcessor)
		{
			void* fxDsp = clapGetReaperContext(host, 4); // sel=4: "FxDsp" -> fxctx
			if (fxDsp)
			{
				state->videoProcessor =
					videoCreateVideoProcessor(fxDsp, IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION);
				if (state->videoProcessor)
				{
					state->videoProcessor->userdata = state;
					state->videoProcessor->process_frame = processFrame;
					state->videoProcessor->get_parameter_value = getVideoParam;
				}
			}
		}

		return true;
	}

	void plugin_deactivate(const clap_plugin_t* plugin)
	{
		auto* state = static_cast<PluginState*>(plugin->plugin_data);
		if (state->videoProcessor)
		{
			delete state->videoProcessor; // MUST delete here, otherwise REAPER keeps calling into a torn-down instance
			state->videoProcessor = nullptr;
		}
	}

	bool plugin_start_processing(const clap_plugin_t*)
	{
		return true;
	}
	void plugin_stop_processing(const clap_plugin_t*) {}
	void plugin_reset(const clap_plugin_t*) {}

	clap_process_status plugin_process(const clap_plugin_t*, const clap_process_t* process)
	{
		// audio path is not ported yet (Phase B) -- passthrough/silence so this is a well-formed track FX
		if (process->audio_outputs_count > 0)
		{
			clap_audio_buffer_t& out = process->audio_outputs[0];
			bool haveInput = process->audio_inputs_count > 0;

			for (uint32_t ch = 0; ch < out.channel_count; ch++)
			{
				if (haveInput && ch < process->audio_inputs[0].channel_count && out.data32 &&
					process->audio_inputs[0].data32)
				{
					std::memcpy(out.data32[ch], process->audio_inputs[0].data32[ch],
								sizeof(float) * process->frames_count);
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

	// -------- clap.audio-ports extension (one stereo in/out, so REAPER treats this as a normal FX) --------

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

	const void* plugin_get_extension(const clap_plugin_t*, const char* id)
	{
		if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
			return &audioPortsExtension;
		return nullptr;
	}

	// -------- factory --------

	const clap_plugin_descriptor_t pluginDescriptor = {
		CLAP_VERSION_INIT,
		kPluginId,
		kPluginName,
		"Emanuele Messina",
		"https://github.com/emanuelemessina/ReaShader",
		"",
		"",
		"0.0.1",
		"THE Video Processor for Reaper (CLAP build system migration, phase A -- rendering not yet ported)",
		nullptr
	};

	const clap_plugin_t* create_plugin(const clap_plugin_factory_t*, const clap_host_t* host,
										const char* pluginId)
	{
		if (std::strcmp(pluginId, kPluginId) != 0)
			return nullptr;

		auto* state = new PluginState();
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

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry = { CLAP_VERSION_INIT, entry_init, entry_deinit,
																  entry_get_factory };
