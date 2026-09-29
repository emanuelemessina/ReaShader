/**
 * @file
 * @brief REAPER's video side as the plugin sees it: frames (IVideoFrame) and the video processor (IREAPERVideoProcessor).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "reaper_sdk.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

namespace host
{
	// A reference-counted frame. The count of live frames is global, so a test can check that the plugin
	// returned or Release()d every frame it got.
	class VideoFrame : public IVideoFrame
	{
	  public:
		VideoFrame(int width, int height, int format, int rowspan);

		void AddRef() override;
		void Release() override;
		char* get_bits() override;
		int get_w() override;
		int get_h() override;
		int get_fmt() override;
		int get_rowspan() override;
		void resize_img(int wantw, int wanth, int wantfmt) override;

		static int liveCount();

	  protected:
		~VideoFrame() override;

	  private:
		int width, height, format, rowspan;
		std::vector<char> bits;
		std::atomic<int> references{ 1 };

		static std::atomic<int> live;
	};

	// The processor REAPER creates for a plugin's FxDsp. The plugin fills process_frame, get_parameter_value
	// and userdata, and deletes it in deactivate.
	class VideoProcessor : public IREAPERVideoProcessor
	{
	  public:
		// onDelete: called from the destructor (the plugin deleted it)
		explicit VideoProcessor(std::function<void()> onDelete);
		~VideoProcessor() override;

		IVideoFrame* newVideoFrame(int w, int h, int fmt) override;
		int getNumInputs() override;
		int getInputInfo(int idx, void** itemptr) override;
		IVideoFrame* renderInputVideoFrame(int idx, int want_fmt) override;

		// the frame renderInputVideoFrame() hands out next (the host keeps its own reference)
		VideoFrame* input = nullptr;

	  private:
		std::function<void()> onDelete;
	};

	// Row stride of the frames the host creates: padded, so the plugin must honor get_rowspan().
	// Assumed: REAPER's actual padding is not verified.
	int rowspanFor(int width);
} // namespace host
