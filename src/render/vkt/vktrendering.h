/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "vktbuffers.h"
#include "vktcommon.h"
#include "vktdescriptors.h"
#include "vktdevices.h"
#include "vktimages.h"
#include "vktvertex.h"

#include "tiny_obj_loader.h"

namespace vkt
{
	namespace Rendering
	{
		class Mesh
		{
		  public:
			Mesh(Logical::Device* vktDevice);

			Mesh* setVertices(std::vector<Vertex> vertices);
			Mesh* setIndices(std::vector<uint32_t> indices);

			/**
			All objects will get merged into one Mesh object.
			*/
			bool load_from_obj(const std::string& filename);

			Buffers::AllocatedBuffer* getVertexBuffer()
			{
				return vertexBuffer;
			}
			Buffers::AllocatedBuffer* getIndexBuffer()
			{
				return indexBuffer;
			}

			std::vector<Vertex> getVertices()
			{
				return vertices;
			}
			std::vector<uint32_t> getIndices()
			{
				return indices;
			}

		  private:
			Logical::Device* vktDevice = nullptr;
			Buffers::AllocatedBuffer* vertexBuffer = nullptr;
			Buffers::AllocatedBuffer* indexBuffer = nullptr;

			std::vector<Vertex> vertices{};
			std::vector<uint32_t> indices{};
		};

		struct Material
		{
			VkPipeline pipeline;
			VkPipelineLayout pipelineLayout;
			VkPipelineCache pipelineCache;

			Material& registerBindDescriptorSets(uint32_t firstSet, uint32_t descriptorSetCount,
												 const VkDescriptorSet* pDescriptorSets, uint32_t dynamicOffsetCount,
												 const uint32_t* pDynamicOffsets)
			{
				// offsets are copied: callers typically pass a local array that's gone by draw time
				registeredDescriptorSets.push_back(
					std::make_tuple(firstSet, descriptorSetCount, pDescriptorSets,
									std::vector<uint32_t>(pDynamicOffsets, pDynamicOffsets + dynamicOffsetCount)));
				return *this;
			}
			// calls vkCmdBindDescriptorSets for all registered sets
			void cmdBindDescriptors(VkCommandBuffer commandBuffer, VkPipelineBindPoint bindPoint)
			{
				for (int i = 0; i < registeredDescriptorSets.size(); i++)
				{
					auto& r = registeredDescriptorSets[i];
					auto& dynamicOffsets = std::get<3>(r);
					vkCmdBindDescriptorSets(commandBuffer, bindPoint, pipelineLayout, std::get<0>(r), std::get<1>(r),
											std::get<2>(r), static_cast<uint32_t>(dynamicOffsets.size()),
											dynamicOffsets.data());
				}
			}

			template <typename P>
			void cmdPushConstants(VkCommandBuffer commandBuffer, VkShaderStageFlags stages, P* constants, uint32_t offset)
			{
				vkCmdPushConstants(commandBuffer, pipelineLayout, stages, 0,
								   sizeof(P), constants);
			}

			private:
			std::vector<std::tuple<uint32_t, uint32_t, const VkDescriptorSet*, std::vector<uint32_t>>>
				registeredDescriptorSets;
		};

		struct RenderObject
		{
			Mesh* mesh = nullptr;

			Material* material;

			glm::mat4 localTransformMatrix = glm::mat4{ 1.0f }; // identity
		};
	} // namespace Rendering

} // namespace vkt
