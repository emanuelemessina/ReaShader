/**
 * @file
 * @brief Unit tests: ReaShaderRenderer's chain on every GPU (order, bypass, params per node, reuse, errors).
 * @author Emanuele Messina (https://github.com/emanuelemessina)
 * @copyright Copyright (c) Emanuele Messina. All rights reserved.
 *            Licensed under the MIT License: see https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE
 */

#include "support/support.h"

#include "plugin/plugin.h"
#include "render/lut_file.h"
#include "render/renderer.h"
#include "render/shader_compiler.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <vector>

using namespace ReaShader;
using Node = ReaShaderRenderer::ChainNode;

namespace
{
	std::shared_ptr<const gpu::CompiledShader> compile(const std::string& source)
	{
		return std::make_shared<const gpu::CompiledShader>(gpu::compileShader(source, "test.glsl"));
	}

	std::shared_ptr<const gpu::CompiledShader> brightnessShader()
	{
		return std::make_shared<const gpu::CompiledShader>(gpu::compileShader(
			test::readFile(test::repoPath("src/shaders/examples/brightness.frag")), "brightness.frag"));
	}

	std::shared_ptr<const gpu::LutData> invertTable()
	{
		return std::make_shared<const gpu::LutData>(test::invertLut());
	}

	Node shaderNode(uint32_t uid, std::shared_ptr<const gpu::CompiledShader> shader, size_t firstParam)
	{
		size_t count = shader->params.size();
		return { .uid = uid, .shader = std::move(shader), .firstParam = firstParam, .paramCount = count };
	}

	Node lutNode(uint32_t uid, std::shared_ptr<const gpu::LutData> lut, size_t mixParam)
	{
		return { .uid = uid, .lut = std::move(lut), .firstParam = mixParam, .paramCount = 1 };
	}

	Node bypassed(Node node)
	{
		node.bypass = true;
		return node;
	}

	double invert(double v)
	{
		return 255 - v;
	}

	// brightness.frag: out = in + brightness * 0.5
	std::function<double(double)> brighten(double brightness)
	{
		return [brightness](double v) { return std::min(255.0, v + brightness * 0.5 * 255); };
	}

	// A renderer the way the plugin runs it, on its own Vulkan instance (the plugin only supplies the device)
	struct TestRenderer
	{
		ReaShaderPlugin plugin;
		ReaShaderRenderer renderer{ &plugin };

		~TestRenderer()
		{
			renderer.shutdown();
		}

		// Runs `body` once per usable GPU, switching the renderer's device before each: the chain carries over.
		// Validation messages logged meanwhile (debug builds) fail the test.
		void forEachGpu(const std::function<void()>& body)
		{
			size_t knownMessages = test::validationMessages().size();
			renderer.init();
			for (size_t i = 0; i < test::gpuCount(); i++)
			{
				INFO("GPU ", i);
				renderer.changeRenderingDevice((int)i);
				body();
			}
			std::vector<std::string> messages = test::validationMessages();
			for (size_t m = knownMessages; m < messages.size(); m++)
				FAIL_CHECK(messages[m]);
		}

		// one frame with `params` as the plugin's param values; false: passed through
		bool render(test::TestFrame& input, test::TestFrame& output, std::vector<double> params = {})
		{
			std::fill(output.bytes.begin(), output.bytes.end(), uint8_t(0));
			ReaShaderRenderer::FrameInputs inputs{ 0.0, 30.0, params.data(), params.size() };
			return renderer.renderFrame(input.view(), output.view(), inputs);
		}
	};
} // namespace

TEST_SUITE("renderer")
{
	TEST_CASE("no chain, or only bypassed nodes: frames pass through")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);

		t.forEachGpu([&] {
			CHECK(t.renderer.setChain({}).empty());
			CHECK_FALSE(t.render(input, output));

			CHECK(t.renderer.setChain({ bypassed(lutNode(1, invertTable(), 0)) }).empty());
			CHECK_FALSE(t.render(input, output, { 1.0 }));
		});
	}

	TEST_CASE("nodes run in order, each with its own params, the same shader twice included")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);
		auto brightness = brightnessShader();
		auto inverted = invertTable();

		t.forEachGpu([&] {
			// params by index: the LUT's Mix, then each shader node's brightness
			REQUIRE(
				t.renderer
					.setChain({ lutNode(5, inverted, 0), shaderNode(2, brightness, 1), shaderNode(3, brightness, 2) })
					.empty());
			REQUIRE(t.render(input, output, { 1.0, 0.2, 0.4 }));
			CHECK(test::mismatches(
					  input, output, [&](double v) { return brighten(0.4)(brighten(0.2)(invert(v))); }, 1.5) == 0);

			// reordered: the same nodes, the same params
			REQUIRE(t.renderer.setChain({ shaderNode(2, brightness, 1), lutNode(5, inverted, 0) }).empty());
			REQUIRE(t.render(input, output, { 1.0, 0.2 }));
			CHECK(test::mismatches(input, output, [&](double v) { return invert(brighten(0.2)(v)); }) == 0);
		});
	}

	TEST_CASE("bypassed nodes are left out of five")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);
		auto brightness = brightnessShader();
		auto inverted = invertTable();

		t.forEachGpu([&] {
			REQUIRE(t.renderer
						.setChain({ lutNode(1, inverted, 0), shaderNode(2, brightness, 1),
									bypassed(lutNode(3, inverted, 0)), bypassed(shaderNode(4, brightness, 2)),
									lutNode(5, inverted, 0) })
						.empty());
			REQUIRE(t.render(input, output, { 1.0, 0.2, 0.4 }));
			CHECK(
				test::mismatches(input, output, [&](double v) { return invert(brighten(0.2)(invert(v))); }, 1.5) == 0);
		});
	}

	TEST_CASE("a LUT node's Mix is its param, 1 when the frame has no value for it")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);

		t.forEachGpu([&] {
			REQUIRE(t.renderer.setChain({ lutNode(1, invertTable(), 0) }).empty());
			REQUIRE(t.render(input, output, { 0.5 }));
			CHECK(test::mismatches(input, output, [](double v) { return (v + invert(v)) / 2; }) == 0);

			REQUIRE(t.render(input, output, {}));
			CHECK(test::mismatches(input, output, invert) == 0);
		});
	}

	TEST_CASE("a shader node's params the frame doesn't have yet get their defaults")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);

		t.forEachGpu([&] {
			REQUIRE(t.renderer.setChain({ shaderNode(1, brightnessShader(), 1) }).empty());

			// as while the plugin waits for the host's rescan: only the params before the node's
			REQUIRE(t.render(input, output, { 0.9 }));
			CHECK(test::mismatches(input, output, brighten(0)) == 0); // brightness defaults to 0

			REQUIRE(t.render(input, output, { 0.9, 0.2 }));
			CHECK(test::mismatches(input, output, brighten(0.2)) == 0);
		});
	}

	TEST_CASE("a shader node samples its own LUT as iChannel1, an identity when it has none")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);
		auto shader = compile("void main() { vec4 c = texture(iChannel0, uv); fragColor = vec4(iLut(c.rgb), c.a); }\n");

		t.forEachGpu([&] {
			Node node = shaderNode(1, shader, 0);
			REQUIRE(t.renderer.setChain({ node }).empty());
			REQUIRE(t.render(input, output));
			CHECK(test::mismatches(input, output, [](double v) { return v; }) == 0);

			node.shaderLut = invertTable();
			REQUIRE(t.renderer.setChain({ node }).empty());
			REQUIRE(t.render(input, output));
			CHECK(test::mismatches(input, output, invert) == 0);
		});
	}

	TEST_CASE("a node keeps its GPU objects while its content is the same: its iFrame goes on")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);
		auto counter = compile("void main() { fragColor = vec4(float(iFrame) / 255.0, 0, 0, 1); }\n");
		auto inverted = invertTable();
		auto frame = [&](std::vector<double> params = { 0.0 }) {
			REQUIRE(t.render(input, output, params));
			return (int)output.at(0, 0)[2]; // R
		};

		t.forEachGpu([&] {
			REQUIRE(t.renderer.setChain({ shaderNode(1, counter, 1) }).empty());
			CHECK(frame() == 0);
			CHECK(frame() == 1);

			// another node added (a LUT at Mix 0, a no-op), then moved before it: the counter is the same pass
			REQUIRE(t.renderer.setChain({ shaderNode(1, counter, 1), lutNode(2, inverted, 0) }).empty());
			CHECK(frame() == 2);
			REQUIRE(t.renderer.setChain({ lutNode(2, inverted, 0), shaderNode(1, counter, 1) }).empty());
			CHECK(frame() == 3);

			// bypassed, it records nothing
			REQUIRE(t.renderer.setChain({ lutNode(2, inverted, 0), bypassed(shaderNode(1, counter, 1)) }).empty());
			frame();
			REQUIRE(t.renderer.setChain({ lutNode(2, inverted, 0), shaderNode(1, counter, 1) }).empty());
			CHECK(frame() == 4);

			// new content, even if equal, or another uid: a new pass
			REQUIRE(t.renderer
						.setChain({ shaderNode(
							1, compile("void main() { fragColor = vec4(float(iFrame) / 255.0, 0, 0, 1); }\n"), 1) })
						.empty());
			CHECK(frame() == 0);
			REQUIRE(t.renderer.setChain({ shaderNode(7, counter, 1) }).empty());
			CHECK(frame() == 0);
		});
	}

	TEST_CASE("an invalid chain is rejected and the current one stays")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);
		auto inverted = invertTable();
		auto brightness = brightnessShader();

		t.forEachGpu([&] {
			REQUIRE(t.renderer.setChain({ lutNode(1, inverted, 0) }).empty());

			Node neither{ .uid = 2 };
			Node both = shaderNode(2, brightness, 0);
			both.lut = inverted;
			std::vector<Node> tooMany;
			for (uint32_t uid = 0; uid <= Parameters::kMaxNodes; uid++)
				tooMany.push_back(lutNode(uid, inverted, 0));

			CHECK_FALSE(t.renderer.setChain({ neither }).empty());
			CHECK_FALSE(t.renderer.setChain({ both }).empty());
			CHECK_FALSE(t.renderer.setChain({ lutNode(3, inverted, 0), shaderNode(3, brightness, 0) }).empty());
			CHECK_FALSE(t.renderer.setChain(tooMany).empty());

			REQUIRE(t.render(input, output, { 1.0 }));
			CHECK(test::mismatches(input, output, invert) == 0);
		});
	}

	TEST_CASE("a chain set before init() is installed by it, and survives device switches")
	{
		TestRenderer t;
		test::TestFrame input = test::gradient(), output(256, 4, 0);
		REQUIRE(t.renderer.setChain({ lutNode(1, invertTable(), 0) }).empty());

		t.forEachGpu([&] {
			REQUIRE(t.render(input, output, { 1.0 }));
			CHECK(test::mismatches(input, output, invert) == 0);
		});
	}
}
