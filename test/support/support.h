/**
 * @file
 * @brief Helpers shared by the test cases: repo files, CPU frames, and running GPU code on every GPU.
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#pragma once

#include "render/context.h"
#include "render/frame_targets.h"
#include "render/frame_view.h"
#include "render/lut.h"
#include "render/scene.h"
#include "render/shader_pass.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace test
{
	// -------- files --------

	// a path inside the repository (REPO_DIR, set by test/CMakeLists.txt)
	std::filesystem::path repoPath(const std::string& relative);
	std::string readFile(const std::filesystem::path& path);

	// -------- frames --------

	// A BGRA frame in CPU memory, rows `rowBytes` apart (padding bytes after each row)
	struct TestFrame
	{
		TestFrame(int width, int height, int padding);

		uint8_t* at(int x, int y);
		ReaShader::FrameView view();

		int width, height, rowBytes;
		std::vector<uint8_t> bytes;
	};

	// 256x4, B = x, G = 255 - x, R = 3x (wrapping), A = 255: every 8-bit value in every color channel
	TestFrame gradient();

	// color channels further than `tolerance` from expected(input value, 0..255); alpha must be the input's
	int mismatches(TestFrame& input, TestFrame& output, const std::function<double(double)>& expected,
				   double tolerance = 1.0);

	// -------- GPU --------

	// Runs `body` once per usable GPU, with that GPU's device created on a Vulkan instance shared by the run.
	// Failures are tagged with the GPU's name. Validation messages logged meanwhile (debug builds) fail the test.
	void forEachGpu(const std::function<void(ReaShader::gpu::Context&)>& body);

	// destroys the shared instance (end of the run)
	void shutdownGpu();

	// the number of usable GPUs
	size_t gpuCount();

	// validation layer messages logged so far in this run (debug builds), one line each
	std::vector<std::string> validationMessages();

	// One frame the way ReaShaderRenderer::renderFrame records it:
	// upload -> passes (FrameTargets::recordPasses; none: a plain copy) -> scene (optional) -> download.
	// LUT passes come with their LUT bound and their amount set by the test.
	struct RenderInputs
	{
		std::vector<ReaShader::gpu::Pass*> passes;
		std::vector<float> params;						// written to the shader passes
		const ReaShader::gpu::Lut* shaderLut = nullptr; // the shader passes' iChannel1 (none: an identity)
		ReaShader::gpu::Scene* scene = nullptr;
		double time = 0;
		double frameRate = 30;
	};
	void render(ReaShader::gpu::Context& context, ReaShader::gpu::FrameTargets& targets, TestFrame& input,
				TestFrame& output, const RenderInputs& inputs);

	// A LUT that inverts every channel. 33 points per side: the hardware's interpolation weights (8-bit on some
	// GPUs) stay far below one 8-bit step, which a 2-point LUT like test/luts/invert.cube would not guarantee.
	ReaShader::gpu::LutData invertLut();
} // namespace test
