#pragma once

#include "vktcommon.h"
#include "vktqueue.h"
#include <ranges>

namespace vkt {

	class CommandPool : IVkWrapper<VkCommandPool> {

	public:
		CommandPool(deletion_queue& deletionQueue, Queue* queue);
		
		/**
		Allocates new command buffer, begins it, then returns it.
		Deletion is up to you.
		*/
		VkCommandBuffer createCommandBuffer();

		/**
		Creates a command buffer, begins it, stores it and pushes to the deletion queue.
		@param pOutCommandBuffer optional pointer to immediately retrieve the command buffer upon creation.
		*/
		CommandPool* createCommandBuffer(const int id, VkCommandBuffer* pOutCommandBuffer = nullptr);

		/**
		Create command buffers, begin them, store them and push them to the deletion queue.
		@param pOutCommandBuffers optional pointers to immediately retrieve the command buffers upon creation.
		*/
		CommandPool* createCommandBuffers(std::vector<int> ids, std::vector<VkCommandBuffer*> pOutCommandBuffers = {});

		/**
		Returns the VkCommandPool
		*/
		VkCommandPool vk()
		{
			return m_commandPool;
		}

		/**
		Returns the VkCommandBuffer with id.
		@param id
		*/
		VkCommandBuffer get(const int id);

		/**
		Resets the previous command buffer provided.
		Then begins it.
		*/
		CommandPool* restartCommandBuffer(const VkCommandBuffer& previousCommandBuffer);

		/**
		Resets and begins the command buffer with id.
		@param id
		*/
		CommandPool* restartCommandBuffer(int id);

		/**
		Calls restartCommandBuffer on all the stored command buffers.
		*/
		CommandPool* restartAll();

	private:

		/**
		Ends the commandBuffer, then submits it (top of pipe).
		*/
		void submitQueueSingle(VkCommandBuffer commandBuffer, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore);

	public:

		/**
		Submits the command buffer specified by id.
		@param id
		*/
		CommandPool* submit(const int id, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore);

		/**
		Submits the command buffer provided.
		*/
		CommandPool* submit(VkCommandBuffer cmd, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore);

	private:

		VkCommandPool m_commandPool = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		deletion_queue* pDeletionQueue;
		Queue* vktQueue;
		vectors::searchable_map<int, VkCommandBuffer> m_commandBuffers;

	};

}
