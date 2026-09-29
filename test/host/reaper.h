/**
 * @file
 * @brief host::Reaper: a fake REAPER that loads a built .clap and drives it the way REAPER does.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

// It is our model of REAPER, as far as we know it. Each emulated behavior is commented "observed" (verified
// in REAPER) or "assumed" (not verified yet). When REAPER turns out to differ, this file changes first.

#include "host/thread.h"

#include <clap/clap.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct reaper_plugin_info_t;
class IREAPERVideoProcessor;

namespace host
{
	class VideoProcessor;

	// A BGRA frame in CPU memory, rows `rowspan` bytes apart
	struct Frame
	{
		Frame() = default;
		Frame(int width, int height, int rowspan);

		uint8_t* at(int x, int y);
		const uint8_t* at(int x, int y) const;

		int width = 0, height = 0, rowspan = 0;
		std::vector<uint8_t> bytes;
	};

	// A plugin parameter, as the host lists it after a (re)scan
	struct Param
	{
		clap_id id;
		std::string name;
		double minValue, maxValue, defaultValue;
		double value; // the host's current value (what automation would send)
	};

	class Reaper
	{
	  public:
		// Loads the .clap and gets its factory. The calling thread is REAPER's main thread.
		explicit Reaper(const std::filesystem::path& clapFile);
		~Reaper(); // destroys the plugin if still alive, then unloads the .clap

		Reaper(const Reaper&) = delete;
		Reaper& operator=(const Reaper&) = delete;

		uint32_t pluginCount() const;
		const clap_plugin_descriptor_t* descriptor() const; // the first plugin's

		// -------- one plugin instance, driven like an FX on a track --------

		void createPlugin();  // create_plugin + init, then the first param scan
		void activate();	  // activate (main) + start_processing (audio)
		void deactivate();	  // stop_processing (audio) + deactivate (main)
		void destroyPlugin(); // deactivates first if needed

		// REAPER's main-thread idle: on_main_thread when requested, a restart when requested
		void idle();

		// clap.state, on the main thread (a project save / load)
		std::string saveState();
		void loadState(const std::string& state);

		// Host automation of a param: the host's value (what process_frame gets) changes now, and the next
		// audio block carries it to the plugin as a CLAP param event
		void automate(clap_id id, double value);

		// `blocks` audio blocks of constant `input` through process(), on the audio thread.
		// Returns the last block's first output channel.
		std::vector<float> processAudio(int blocks, float input);

		// One video frame through process_frame(), on the video thread
		struct VideoResult
		{
			Frame frame;			 // what the plugin returned
			bool passthrough = false; // it returned the input frame itself
		};
		VideoResult renderVideo(const Frame& input, double time);

		// -------- observations --------

		const std::vector<Param>& params() const;
		const Param* param(const std::string& name) const; // by name, null if absent
		double pluginValue(clap_id id) const;			   // params.get_value: the plugin's own value
		bool isActive() const;
		bool hasVideoProcessor() const;
		int restarts() const; // restarts done for request_restart
		int rescans() const;

		// Everything that went wrong, empty when fine: message boxes the plugin showed (closed by the host),
		// validation messages in the plugin's rs.log (debug builds), and broken host contracts
		// (spec violations, unknown REAPER functions, leaked frames).
		std::vector<std::string> problems() const;

	  private:
		void problem(std::string what) const;
		void scanParams();
		void watchForMessageBoxes();

		// clap_host_t callbacks
		static const void* getExtension(const clap_host_t* clapHost, const char* id);
		static void requestRestart(const clap_host_t* clapHost);
		static void requestProcess(const clap_host_t* clapHost);
		static void requestCallback(const clap_host_t* clapHost);
		static void paramsRescan(const clap_host_t* clapHost, clap_param_rescan_flags flags);
		static void paramsClear(const clap_host_t* clapHost, clap_id id, clap_param_clear_flags flags);
		static void paramsRequestFlush(const clap_host_t* clapHost);

		// REAPER API, through reaper_plugin_info_t::GetFunc
		static void* getFunc(const char* name);
		static void* clapGetReaperContext(const clap_host_t* clapHost, int sel);
		static IREAPERVideoProcessor* videoCreateVideoProcessor(void* fxDsp, int version);
		static void* getSetMediaTrackInfo(void* track, const char* parm, void* setNewValue);
		static double getMediaTrackInfoValue(void* track, const char* parm);

		static Reaper* current; // GetFunc has no context: one Reaper at a time

		std::filesystem::path clapFile;
		void* module = nullptr; // HMODULE
		const clap_plugin_entry_t* entry = nullptr;
		const clap_plugin_factory_t* factory = nullptr;

		clap_host_t clapHost{};
		std::unique_ptr<reaper_plugin_info_t> reaperInfo;
		struct Context
		{
			Reaper* reaper;
		} fxDsp{ this }, track{ this }; // what clap_get_reaper_context hands out
		std::string trackName = "Test track";

		const clap_plugin_t* plugin = nullptr;
		const clap_plugin_params_t* pluginParams = nullptr;
		std::vector<Param> paramList;
		std::mutex automationMutex;
		std::vector<clap_event_param_value_t> automation; // for the next audio block
		bool active = false;

		std::thread::id mainThread;
		HostThread audioThread, videoThread;

		std::atomic<bool> callbackRequested{ false }, restartRequested{ false }, flushRequested{ false };
		std::atomic<int> restartCount{ 0 }, rescanCount{ 0 };
		std::atomic<VideoProcessor*> videoProcessor{ nullptr };

		mutable std::mutex problemsMutex;
		mutable std::vector<std::string> problemList;

		std::atomic<bool> watching{ true };
		std::thread boxWatcher;
	};

	// The built plugin this test build goes with (set by test/CMakeLists.txt)
	std::filesystem::path builtPlugin();
} // namespace host
