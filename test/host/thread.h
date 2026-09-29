/**
 * @file
 * @brief host::HostThread: one of REAPER's threads (audio, video).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

namespace host
{
	// One of REAPER's threads (audio, video): runs calls on its own OS thread, one at a time.
	// run() waits for the call and rethrows its exception, so a test reads like straight-line code
	// while the plugin still sees the calls come from that thread.
	class HostThread
	{
	  public:
		HostThread() : thread([this] { loop(); }) {}

		~HostThread()
		{
			{
				std::lock_guard lock(mutex);
				stopping = true;
			}
			wake.notify_all();
			thread.join();
		}

		HostThread(const HostThread&) = delete;
		HostThread& operator=(const HostThread&) = delete;

		void run(const std::function<void()>& call)
		{
			std::unique_lock lock(mutex);
			pending = &call;
			error = nullptr;
			wake.notify_all();
			done.wait(lock, [this] { return pending == nullptr; });
			if (error)
				std::rethrow_exception(error);
		}

		std::thread::id id() const
		{
			return thread.get_id();
		}

	  private:
		void loop()
		{
			std::unique_lock lock(mutex);
			while (true)
			{
				wake.wait(lock, [this] { return pending || stopping; });
				if (!pending)
					return;
				try
				{
					(*pending)();
				}
				catch (...)
				{
					error = std::current_exception();
				}
				pending = nullptr;
				done.notify_all();
			}
		}

		std::mutex mutex;
		std::condition_variable wake, done;
		const std::function<void()>* pending = nullptr;
		std::exception_ptr error;
		bool stopping = false;
		std::thread thread; // last: started after the members it uses
	};
} // namespace host
