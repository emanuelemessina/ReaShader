/**
 * @file
 * @brief REAPER's video frames and video processor, as the host provides them.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "host/video.h"

namespace host
{
	// -------- VideoFrame --------

	std::atomic<int> VideoFrame::live{ 0 };

	VideoFrame::VideoFrame(int frameWidth, int frameHeight, int frameFormat, int frameRowspan)
		: width(frameWidth), height(frameHeight), format(frameFormat), rowspan(frameRowspan),
		  bits((size_t)frameRowspan * (size_t)frameHeight)
	{
		time_start = 0;
		time_end = 0;
		live++;
	}

	VideoFrame::~VideoFrame()
	{
		live--;
	}

	void VideoFrame::AddRef()
	{
		references++;
	}

	void VideoFrame::Release()
	{
		if (--references == 0)
			delete this;
	}

	char* VideoFrame::get_bits()
	{
		return bits.data();
	}

	int VideoFrame::get_w()
	{
		return width;
	}

	int VideoFrame::get_h()
	{
		return height;
	}

	int VideoFrame::get_fmt()
	{
		return format;
	}

	int VideoFrame::get_rowspan()
	{
		return rowspan;
	}

	void VideoFrame::resize_img(int wantw, int wanth, int wantfmt)
	{
		width = wantw;
		height = wanth;
		format = wantfmt;
		rowspan = rowspanFor(wantw);
		bits.assign((size_t)rowspan * (size_t)height, 0);
	}

	int VideoFrame::liveCount()
	{
		return live;
	}

	// -------- VideoProcessor --------

	VideoProcessor::VideoProcessor(std::function<void()> onDeleted) : onDelete(std::move(onDeleted)) {}

	VideoProcessor::~VideoProcessor()
	{
		if (onDelete)
			onDelete();
	}

	IVideoFrame* VideoProcessor::newVideoFrame(int w, int h, int fmt)
	{
		return new VideoFrame(w, h, fmt, rowspanFor(w));
	}

	int VideoProcessor::getNumInputs()
	{
		return 1;
	}

	int VideoProcessor::getInputInfo(int, void** itemptr)
	{
		if (itemptr)
			*itemptr = nullptr;
		return 0;
	}

	// Assumed: REAPER hands out the upstream frame with an extra reference, in the requested format
	// (the host only has 'RGBA' frames).
	IVideoFrame* VideoProcessor::renderInputVideoFrame(int idx, int)
	{
		if (idx != 0 || !input)
			return nullptr;
		input->AddRef();
		return input;
	}

	int rowspanFor(int width)
	{
		return width * 4 + 64;
	}
} // namespace host
