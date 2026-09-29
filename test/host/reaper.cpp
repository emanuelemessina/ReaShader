/**
 * @file
 * @brief host::Reaper: the fake REAPER host.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "host/reaper.h"

#include "host/reaper_sdk.h"
#include "host/video.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <stdexcept>

namespace host
{
	Reaper* Reaper::current = nullptr;

	namespace
	{
		constexpr double kSampleRate = 48000;
		constexpr uint32_t kBlockSize = 256;
		constexpr double kFrameRate = 30;

		// -------- event lists for process() / flush() --------

		struct InputEvents
		{
			clap_input_events_t list{ this, size, get };
			std::vector<clap_event_param_value_t> events;

			static uint32_t size(const clap_input_events_t* list)
			{
				return (uint32_t)static_cast<const InputEvents*>(list->ctx)->events.size();
			}
			static const clap_event_header_t* get(const clap_input_events_t* list, uint32_t index)
			{
				return &static_cast<const InputEvents*>(list->ctx)->events.at(index).header;
			}
		};

		struct OutputEvents
		{
			clap_output_events_t list{ this, tryPush };
			std::vector<clap_event_param_value_t> paramValues;

			static bool tryPush(const clap_output_events_t* list, const clap_event_header_t* event)
			{
				if (event->space_id == CLAP_CORE_EVENT_SPACE_ID && event->type == CLAP_EVENT_PARAM_VALUE)
					static_cast<OutputEvents*>(list->ctx)->paramValues.push_back(
						*reinterpret_cast<const clap_event_param_value_t*>(event));
				return true;
			}
		};

		// -------- message boxes --------

		struct DialogSearch
		{
			DWORD process;
			std::vector<HWND> dialogs;
		};

		BOOL CALLBACK collectDialog(HWND window, LPARAM param)
		{
			auto* search = reinterpret_cast<DialogSearch*>(param);
			DWORD process = 0;
			GetWindowThreadProcessId(window, &process);
			wchar_t className[32]{};
			GetClassNameW(window, className, 32);
			if (process == search->process && std::wcscmp(className, L"#32770") == 0 && IsWindowVisible(window))
				search->dialogs.push_back(window);
			return TRUE;
		}

		std::string narrow(const std::wstring& text)
		{
			int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
			std::string result((size_t)size, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), result.data(), size, nullptr, nullptr);
			return result;
		}

		std::string windowText(HWND window)
		{
			wchar_t text[1024]{};
			GetWindowTextW(window, text, 1024);
			return narrow(text);
		}

		BOOL CALLBACK collectStaticText(HWND window, LPARAM param)
		{
			wchar_t className[32]{};
			GetClassNameW(window, className, 32);
			if (std::wcscmp(className, L"Static") == 0)
			{
				std::string text = windowText(window);
				if (!text.empty())
					*reinterpret_cast<std::string*>(param) += text;
			}
			return TRUE;
		}
	} // namespace

	// -------- Frame --------

	Frame::Frame(int frameWidth, int frameHeight, int frameRowspan)
		: width(frameWidth), height(frameHeight), rowspan(frameRowspan),
		  bytes((size_t)frameRowspan * (size_t)frameHeight)
	{
	}

	uint8_t* Frame::at(int x, int y)
	{
		return &bytes[(size_t)y * (size_t)rowspan + (size_t)x * 4];
	}

	const uint8_t* Frame::at(int x, int y) const
	{
		return &bytes[(size_t)y * (size_t)rowspan + (size_t)x * 4];
	}

	// -------- loading --------

	Reaper::Reaper(const std::filesystem::path& clapPath) : clapFile(clapPath), mainThread(std::this_thread::get_id())
	{
		if (current)
			throw std::logic_error("Only one Reaper at a time");
		current = this;

		// a fresh log: the plugin appends validation messages to it (debug builds)
		std::error_code ignored;
		std::filesystem::remove(clapFile.parent_path() / "rs.log", ignored);

		clapHost = { CLAP_VERSION_INIT, this, "REAPER", "Cockos", "https://www.reaper.fm", "7",
					 getExtension, requestRestart, requestProcess, requestCallback };

		reaperInfo = std::make_unique<reaper_plugin_info_t>();
		*reaperInfo = {};
		reaperInfo->caller_version = REAPER_PLUGIN_VERSION;
		reaperInfo->Register = [](const char*, void*) { return 0; };
		reaperInfo->GetFunc = getFunc;

		boxWatcher = std::thread([this] { watchForMessageBoxes(); });

		module = LoadLibraryW(clapFile.wstring().c_str());
		if (!module)
			throw std::runtime_error(std::format("Can't load {} (error {})", clapFile.string(), GetLastError()));

		entry = reinterpret_cast<const clap_plugin_entry_t*>(GetProcAddress((HMODULE)module, "clap_entry"));
		if (!entry)
			throw std::runtime_error("No clap_entry in " + clapFile.string());
		if (!entry->init(clapFile.string().c_str()))
			throw std::runtime_error("clap_entry.init failed");

		factory = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
		if (!factory)
			throw std::runtime_error("No plugin factory");
	}

	Reaper::~Reaper()
	{
		if (plugin)
			destroyPlugin();
		if (entry)
			entry->deinit();
		if (module)
			FreeLibrary((HMODULE)module);

		watching = false;
		boxWatcher.join();
		current = nullptr;
	}

	uint32_t Reaper::pluginCount() const
	{
		return factory->get_plugin_count(factory);
	}

	const clap_plugin_descriptor_t* Reaper::descriptor() const
	{
		return factory->get_plugin_descriptor(factory, 0);
	}

	// -------- lifecycle --------

	void Reaper::createPlugin()
	{
		plugin = factory->create_plugin(factory, &clapHost, descriptor()->id);
		if (!plugin)
			throw std::runtime_error("create_plugin failed");
		if (!plugin->init(plugin))
			throw std::runtime_error("plugin init failed");
		pluginParams = static_cast<const clap_plugin_params_t*>(plugin->get_extension(plugin, CLAP_EXT_PARAMS));
		scanParams();
	}

	// Assumed: REAPER activates on the main thread, then starts processing on the audio thread.
	void Reaper::activate()
	{
		if (!plugin->activate(plugin, kSampleRate, 1, kBlockSize))
			throw std::runtime_error("plugin activate failed");
		active = true;
		audioThread.run([this] { plugin->start_processing(plugin); });
	}

	// Assumed: the plugin counts as inactive from the start of deactivate (it rescans params in there).
	void Reaper::deactivate()
	{
		audioThread.run([this] { plugin->stop_processing(plugin); });
		active = false;
		plugin->deactivate(plugin);
	}

	void Reaper::destroyPlugin()
	{
		if (active)
			deactivate();
		plugin->destroy(plugin);
		plugin = nullptr;
		pluginParams = nullptr;
		paramList.clear();
	}

	// Assumed: REAPER answers these from its main-thread timer, in this order, and restarts an active
	// plugin with deactivate + activate.
	void Reaper::idle()
	{
		for (int round = 0; round < 8; round++) // until nothing is pending (a callback can request more)
		{
			bool didSomething = false;
			if (callbackRequested.exchange(false))
			{
				plugin->on_main_thread(plugin);
				didSomething = true;
			}
			if (restartRequested.exchange(false))
			{
				if (active)
				{
					deactivate();
					activate();
					restartCount++;
				}
				didSomething = true;
			}
			if (flushRequested.exchange(false))
			{
				// when processing, process() would carry the events; a short block does the same
				if (active)
					processAudio(1, 0.0f);
				else if (pluginParams)
				{
					InputEvents in;
					OutputEvents out;
					pluginParams->flush(plugin, &in.list, &out.list);
					for (const clap_event_param_value_t& event : out.paramValues)
						for (Param& param : paramList)
							if (param.id == event.param_id)
								param.value = event.value;
				}
				didSomething = true;
			}
			if (!didSomething)
				return;
		}
		problem("idle: the plugin keeps requesting main-thread work");
	}

	// -------- audio --------

	std::vector<float> Reaper::processAudio(int blocks, float input)
	{
		std::vector<float> inputs[2], outputs[2];
		for (int ch = 0; ch < 2; ch++)
		{
			inputs[ch].assign(kBlockSize, input);
			outputs[ch].assign(kBlockSize, 0.0f);
		}
		float* inputChannels[2] = { inputs[0].data(), inputs[1].data() };
		float* outputChannels[2] = { outputs[0].data(), outputs[1].data() };

		audioThread.run([&] {
			for (int block = 0; block < blocks; block++)
			{
				clap_audio_buffer_t audioIn{ inputChannels, nullptr, 2, 0, 0 };
				clap_audio_buffer_t audioOut{ outputChannels, nullptr, 2, 0, 0 };
				InputEvents in;
				OutputEvents out;
				if (block == 0)
				{
					std::lock_guard lock(automationMutex);
					in.events.swap(automation);
				}

				clap_process_t process{};
				process.steady_time = -1;
				process.frames_count = kBlockSize;
				process.audio_inputs = &audioIn;
				process.audio_outputs = &audioOut;
				process.audio_inputs_count = 1;
				process.audio_outputs_count = 1;
				process.in_events = &in.list;
				process.out_events = &out.list;
				plugin->process(plugin, &process);

				for (const clap_event_param_value_t& event : out.paramValues)
					for (Param& param : paramList)
						if (param.id == event.param_id)
							param.value = event.value;
			}
		});
		return outputs[0];
	}

	// -------- state --------

	namespace
	{
		struct OutputStream
		{
			clap_ostream_t stream{ this, write };
			std::string data;

			static int64_t write(const clap_ostream_t* stream, const void* buffer, uint64_t size)
			{
				static_cast<OutputStream*>(stream->ctx)->data.append(static_cast<const char*>(buffer), (size_t)size);
				return (int64_t)size;
			}
		};

		// Assumed: REAPER hands the state over in chunks; small ones exercise the plugin's read loop
		struct InputStream
		{
			clap_istream_t stream{ this, read };
			const std::string& data;
			size_t position = 0;

			explicit InputStream(const std::string& state) : data(state) {}

			static int64_t read(const clap_istream_t* stream, void* buffer, uint64_t size)
			{
				auto* self = static_cast<InputStream*>(stream->ctx);
				size_t count = std::min({ (size_t)size, (size_t)1000, self->data.size() - self->position });
				std::memcpy(buffer, self->data.data() + self->position, count);
				self->position += count;
				return (int64_t)count;
			}
		};
	} // namespace

	std::string Reaper::saveState()
	{
		auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
		OutputStream out;
		if (!state || !state->save(plugin, &out.stream))
			problem("state.save failed");
		return out.data;
	}

	void Reaper::loadState(const std::string& data)
	{
		auto* state = static_cast<const clap_plugin_state_t*>(plugin->get_extension(plugin, CLAP_EXT_STATE));
		InputStream in(data);
		if (!state || !state->load(plugin, &in.stream))
			problem("state.load failed");
	}

	// -------- automation --------

	void Reaper::automate(clap_id id, double value)
	{
		for (Param& param : paramList)
			if (param.id == id)
				param.value = value;

		clap_event_param_value_t event{};
		event.header.size = sizeof(event);
		event.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
		event.header.type = CLAP_EVENT_PARAM_VALUE;
		event.param_id = id;
		event.note_id = -1;
		event.port_index = -1;
		event.channel = -1;
		event.key = -1;
		event.value = value;

		std::lock_guard lock(automationMutex);
		automation.push_back(event);
	}

	// -------- video --------

	// Assumed: REAPER calls process_frame with force_format 0, wet/dry 1, and the plugin's params in their
	// CLAP index order after it; it Release()s the returned frame once it has used it.
	Reaper::VideoResult Reaper::renderVideo(const Frame& input, double time)
	{
		VideoResult result;
		videoThread.run([&] {
			VideoProcessor* processor = videoProcessor;
			if (!processor || !processor->process_frame)
			{
				problem("renderVideo: no video processor");
				return;
			}

			auto* inputFrame = new VideoFrame(input.width, input.height, 'RGBA', input.rowspan);
			std::memcpy(inputFrame->get_bits(), input.bytes.data(), input.bytes.size());
			processor->input = inputFrame;

			std::vector<double> parmlist{ 1.0 };
			for (const Param& param : paramList)
				parmlist.push_back(param.value);

			IVideoFrame* output =
				processor->process_frame(processor, parmlist.data(), (int)parmlist.size(), time, kFrameRate, 0);

			if (!output)
				problem("process_frame returned no frame");
			else
			{
				result.passthrough = output == inputFrame;
				result.frame = Frame(output->get_w(), output->get_h(), output->get_rowspan());
				std::memcpy(result.frame.bytes.data(), output->get_bits(), result.frame.bytes.size());
				output->Release();
			}

			processor->input = nullptr;
			inputFrame->Release();
			if (int leaked = VideoFrame::liveCount())
				problem(std::format("process_frame left {} frame(s) unreleased", leaked));
		});
		return result;
	}

	// -------- observations --------

	const std::vector<Param>& Reaper::params() const
	{
		return paramList;
	}

	const Param* Reaper::param(const std::string& name) const
	{
		for (const Param& p : paramList)
			if (p.name == name)
				return &p;
		return nullptr;
	}

	double Reaper::pluginValue(clap_id id) const
	{
		double value = 0;
		if (!pluginParams || !pluginParams->get_value(plugin, id, &value))
			problem(std::format("params.get_value({}) failed", id));
		return value;
	}

	bool Reaper::isActive() const
	{
		return active;
	}

	bool Reaper::hasVideoProcessor() const
	{
		return videoProcessor != nullptr;
	}

	int Reaper::restarts() const
	{
		return restartCount;
	}

	int Reaper::rescans() const
	{
		return rescanCount;
	}

	std::vector<std::string> Reaper::problems() const
	{
		std::vector<std::string> all;
		{
			std::lock_guard lock(problemsMutex);
			all = problemList;
		}

		std::ifstream log(clapFile.parent_path() / "rs.log");
		std::string line;
		while (std::getline(log, line))
			if (line.find("(Vulkan) Validation") != std::string::npos)
				all.push_back(line);
		return all;
	}

	void Reaper::problem(std::string what) const
	{
		std::lock_guard lock(problemsMutex);
		problemList.push_back(std::move(what));
	}

	void Reaper::scanParams()
	{
		std::vector<Param> scanned;
		uint32_t count = pluginParams ? pluginParams->count(plugin) : 0;
		for (uint32_t i = 0; i < count; i++)
		{
			clap_param_info_t info{};
			if (!pluginParams->get_info(plugin, i, &info))
			{
				problem(std::format("params.get_info({}) failed", i));
				continue;
			}
			double value = info.default_value;
			if (!pluginParams->get_value(plugin, info.id, &value))
				problem(std::format("params.get_value({}) failed", info.id));
			scanned.push_back({ info.id, info.name, info.min_value, info.max_value, info.default_value, value });
		}
		paramList = std::move(scanned);
	}

	// The plugin's message boxes are modal and would hang the run: close each one and report it
	void Reaper::watchForMessageBoxes()
	{
		std::vector<HWND> reported;
		while (watching)
		{
			DialogSearch search{ GetCurrentProcessId(), {} };
			EnumWindows(collectDialog, reinterpret_cast<LPARAM>(&search));
			for (HWND dialog : search.dialogs)
			{
				if (std::find(reported.begin(), reported.end(), dialog) != reported.end())
					continue; // closing
				reported.push_back(dialog);
				std::string text;
				EnumChildWindows(dialog, collectStaticText, reinterpret_cast<LPARAM>(&text));
				problem("message box: " + windowText(dialog) + ": " + text);
				PostMessageW(dialog, WM_CLOSE, 0, 0);
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(20));
		}
	}

	// -------- clap_host_t --------

	const void* Reaper::getExtension(const clap_host_t* clapHost, const char* id)
	{
		auto* reaper = static_cast<Reaper*>(clapHost->host_data);
		static const clap_host_params_t hostParams{ paramsRescan, paramsClear, paramsRequestFlush };

		if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
			return &hostParams;
		// observed (the plugin's video tap works in REAPER): a CLAP plugin reaches the REAPER API through this extension
		if (std::strcmp(id, "cockos.reaper_extension") == 0)
			return reaper->reaperInfo.get();
		return nullptr;
	}

	void Reaper::requestRestart(const clap_host_t* clapHost)
	{
		static_cast<Reaper*>(clapHost->host_data)->restartRequested = true;
	}

	void Reaper::requestProcess(const clap_host_t*) {}

	void Reaper::requestCallback(const clap_host_t* clapHost)
	{
		static_cast<Reaper*>(clapHost->host_data)->callbackRequested = true;
	}

	// CLAP: rescan(ALL) only while deactivated, on the main thread
	void Reaper::paramsRescan(const clap_host_t* clapHost, clap_param_rescan_flags flags)
	{
		auto* reaper = static_cast<Reaper*>(clapHost->host_data);
		if (std::this_thread::get_id() != reaper->mainThread)
			reaper->problem("params.rescan called off the main thread");
		if ((flags & CLAP_PARAM_RESCAN_ALL) && reaper->active)
			reaper->problem("params.rescan(ALL) called while active");
		reaper->rescanCount++;
		reaper->scanParams();
	}

	void Reaper::paramsClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}

	void Reaper::paramsRequestFlush(const clap_host_t* clapHost)
	{
		static_cast<Reaper*>(clapHost->host_data)->flushRequested = true;
	}

	// -------- REAPER API --------

	void* Reaper::getFunc(const char* name)
	{
		if (std::strcmp(name, "clap_get_reaper_context") == 0)
			return reinterpret_cast<void*>(clapGetReaperContext);
		if (std::strcmp(name, "video_CreateVideoProcessor") == 0)
			return reinterpret_cast<void*>(videoCreateVideoProcessor);
		if (std::strcmp(name, "GetSetMediaTrackInfo") == 0)
			return reinterpret_cast<void*>(getSetMediaTrackInfo);
		if (std::strcmp(name, "GetMediaTrackInfo_Value") == 0)
			return reinterpret_cast<void*>(getMediaTrackInfoValue);

		if (current)
			current->problem(std::format("GetFunc(\"{}\"): not emulated by the host", name));
		return nullptr;
	}

	// observed (the plugin's video tap works in REAPER): 4 = the FX's FxDsp context (for
	// video_CreateVideoProcessor), 1 = its track
	void* Reaper::clapGetReaperContext(const clap_host_t* clapHost, int sel)
	{
		auto* reaper = static_cast<Reaper*>(clapHost->host_data);
		if (sel == 4)
			return &reaper->fxDsp;
		if (sel == 1)
			return &reaper->track;
		return nullptr;
	}

	IREAPERVideoProcessor* Reaper::videoCreateVideoProcessor(void* fxDspContext, int version)
	{
		Reaper* reaper = static_cast<Context*>(fxDspContext)->reaper;
		if (fxDspContext != &reaper->fxDsp)
			reaper->problem("video_CreateVideoProcessor: not an FxDsp context");
		if (version != IREAPERVideoProcessor::REAPER_VIDEO_PROCESSOR_VERSION)
			reaper->problem(std::format("video_CreateVideoProcessor: unknown version {:#x}", version));
		if (reaper->videoProcessor)
			reaper->problem("video_CreateVideoProcessor: the previous processor wasn't deleted");

		auto* processor = new VideoProcessor([reaper] { reaper->videoProcessor = nullptr; });
		reaper->videoProcessor = processor;
		return processor;
	}

	void* Reaper::getSetMediaTrackInfo(void* trackContext, const char* parm, void* setNewValue)
	{
		Reaper* reaper = static_cast<Context*>(trackContext)->reaper;
		if (setNewValue)
			reaper->problem(std::format("GetSetMediaTrackInfo: the plugin sets {}", parm));
		if (std::strcmp(parm, "P_NAME") == 0)
			return const_cast<char*>(reaper->trackName.c_str());
		return nullptr;
	}

	double Reaper::getMediaTrackInfoValue(void*, const char* parm)
	{
		if (std::strcmp(parm, "IP_TRACKNUMBER") == 0)
			return 1;
		return 0;
	}

	std::filesystem::path builtPlugin()
	{
		return REASHADER_CLAP;
	}
} // namespace host
