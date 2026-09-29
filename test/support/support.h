/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

// Helpers shared by the test cases: repo files, CPU frames, and running GPU code on every GPU.

#include "render/context.h"
#include "render/frame_targets.h"
#include "render/frame_view.h"
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

	// -------- GPU --------

	// Runs `body` once per usable GPU, with that GPU's device created on a Vulkan instance shared by the run.
	// Failures are tagged with the GPU's name. Validation messages logged meanwhile (debug builds) fail the test.
	void forEachGpu(const std::function<void(ReaShader::gpu::Context&)>& body);

	// destroys the shared instance (end of the run)
	void shutdownGpu();

	// One frame the way ReaShaderRenderer::renderFrame records it:
	// upload -> shader pass (or a plain copy) -> scene (optional) -> download
	struct RenderInputs
	{
		ReaShader::gpu::ShaderPass* pass = nullptr;
		std::vector<float> params;
		ReaShader::gpu::Scene* scene = nullptr;
		double time = 0;
		double frameRate = 30;
	};
	void render(ReaShader::gpu::Context& context, ReaShader::gpu::FrameTargets& targets, TestFrame& input,
				TestFrame& output, const RenderInputs& inputs);
} // namespace test
