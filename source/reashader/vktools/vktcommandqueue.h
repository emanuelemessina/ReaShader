#pragma once

#include "vktools.h"

#include <ranges>

namespace vkt {

	class vktQueue {
	public:
		vktQueue(vktDeletionQueue& deletionQueue, VkDevice device, uint32_t queueFamilyIndex);

		VkQueue get();
		uint32_t getFamilyIndex();

		void waitIdle();

		friend class vktCommandPool;

	private:
		VkDevice device = VK_NULL_HANDLE;
		VkQueue queue = VK_NULL_HANDLE;
		std::optional<uint32_t> queueFamilyIndex;
	};

	class vktCommandPool {

	public:
		vktCommandPool(vktDeletionQueue& deletionQueue, vktQueue* queue);
		~vktCommandPool() {
		}

		/**
		Allocates new command buffer, begins it, then returns it.
		Deletion is up to you.
		*/
		VkCommandBuffer createCommandBuffer();

		/**
		Creates a command buffer, begins it, stores it and pushes to the deletion queue.
		@param pOutCommandBuffer optional pointer to immediately retrieve the command buffer upon creation.
		*/
		vktCommandPool* createCommandBuffer(const int id, VkCommandBuffer* pOutCommandBuffer = nullptr);

		/**
		Create command buffers, begin them, store them and push them to the deletion queue.
		@param pOutCommandBuffers optional pointers to immediately retrieve the command buffers upon creation.
		*/
		vktCommandPool* createCommandBuffers(std::vector<int> ids, std::vector<VkCommandBuffer*> pOutCommandBuffers = {});

		/**
		Returns the VkCommandPool
		*/
		VkCommandPool get();

		/**
		Returns the VkCommandBuffer with id.
		@param id
		*/
		VkCommandBuffer get(const int id);

		/**
		Resets the previous command buffer provided.
		Then begins it.
		*/
		vktCommandPool* restartCommandBuffer(const VkCommandBuffer& previousCommandBuffer);

		/**
		Resets and begins the command buffer with id.
		@param id
		*/
		vktCommandPool* restartCommandBuffer(int id);

		/**
		Calls restartCommandBuffer on all the stored command buffers.
		*/
		vktCommandPool* restartAll();

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
		vktCommandPool* submit(const int id, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore);

		/**
		Submits the command buffer provided.
		*/
		vktCommandPool* submit(VkCommandBuffer cmd, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore);

	private:

		VkCommandPool m_commandPool = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		vktDeletionQueue* pDeletionQueue;
		vktQueue* vktQueue;
		vectors::searchable_map<int, VkCommandBuffer> m_commandBuffers;

	};

}
