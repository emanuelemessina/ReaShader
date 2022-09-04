#include "vktcommandqueue.h"

namespace vkt {

	// class vktQueue

	vktQueue::vktQueue(vktDeletionQueue& deletionQueue, VkDevice device, uint32_t queueFamilyIndex)
		: queueFamilyIndex(queueFamilyIndex), device(device) {
		deletionQueue.push_function([=]() { delete(this); });
		vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);
	}

	VkQueue vktQueue::get() { return queue; };
	uint32_t vktQueue::getFamilyIndex() { return queueFamilyIndex.value(); }

	void vktQueue::waitIdle() {
		VK_CHECK_RESULT(vkQueueWaitIdle(queue));
	}

	// clasee vktQueue
	//------------------------------------


	// class vktCommandPool

	vktCommandPool::vktCommandPool(vktDeletionQueue& deletionQueue, vkt::vktQueue* queue)
		: device(queue->device), vktQueue(queue), pDeletionQueue(&deletionQueue) {

		deletionQueue.push_function([=]() { delete(this); });

		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = queue->getFamilyIndex();

		if (vkCreateCommandPool(device, &poolInfo, nullptr, &m_commandPool) != VK_SUCCESS) {
			throw std::runtime_error("failed to create command pool!");
		}

		deletionQueue.push_function([=]() {
			vkDestroyCommandPool(device, m_commandPool, nullptr);
			});
	}

	/**
	Allocates new command buffer, begins it, then returns it.
	Deletion is up to you.
	*/
	VkCommandBuffer vktCommandPool::createCommandBuffer() {

		VkCommandBuffer commandBuffer;

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;

		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0; // Optional
		beginInfo.pInheritanceInfo = nullptr; // Optional

		VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginInfo));

		return commandBuffer;
	}

	/**
	Creates a command buffer, begins it, stores it and pushes to the deletion queue.
	@param pOutCommandBuffer optional pointer to immediately retrieve the command buffer upon creation.
	*/
	vktCommandPool* vktCommandPool::createCommandBuffer(const int id, VkCommandBuffer* pOutCommandBuffer) {
		return createCommandBuffers({ id }, { pOutCommandBuffer });
	}
	/**
	Create command buffers, begin them, store them and push them to the deletion queue.
	@param pOutCommandBuffers optional pointers to immediately retrieve the command buffers upon creation.
	*/
	vktCommandPool* vktCommandPool::createCommandBuffers(std::vector<int> ids, std::vector<VkCommandBuffer*> pOutCommandBuffers) {

		uint32_t count = static_cast<uint32_t>(ids.size());

		std::vector<VkCommandBuffer>commandBuffers(count);

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = count;

		VK_CHECK_RESULT(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()));

		pDeletionQueue->push_function([=]() {
			vkFreeCommandBuffers(device, m_commandPool, 1, commandBuffers.data());
			});

		for (uint32_t i = 0; i < count; i++) {

			// store

			m_commandBuffers.add(ids[i], commandBuffers[i]);

			// begin

			VkCommandBufferBeginInfo beginInfo{};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = 0; // Optional
			beginInfo.pInheritanceInfo = nullptr; // Optional

			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffers[i], &beginInfo));

		}

		// out

		if (!pOutCommandBuffers.empty())
			for (uint32_t i = 0; i < count; i++) {
				*pOutCommandBuffers[i] = commandBuffers[i];
			}

		return this;
	}

	/**
	Returns the VkCommandPool
	*/
	VkCommandPool vktCommandPool::get() { return m_commandPool; }
	/**
	Returns the VkCommandBuffer with id.
	@param id
	*/
	VkCommandBuffer vktCommandPool::get(const int id) { return *m_commandBuffers.get(id); }

	/**
	Resets the previous command buffer provided.
	Then begins it.
	*/
	vktCommandPool* vktCommandPool::restartCommandBuffer(const VkCommandBuffer& previousCommandBuffer) {

		vkResetCommandBuffer(previousCommandBuffer, 0);

		VkCommandBufferBeginInfo beginInfo{};
		beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		beginInfo.flags = 0; // Optional
		beginInfo.pInheritanceInfo = nullptr; // Optional

		VK_CHECK_RESULT(vkBeginCommandBuffer(previousCommandBuffer, &beginInfo));

		return this;
	}
	/**
	Resets and begins the command buffer with id.
	@param id
	*/
	vktCommandPool* vktCommandPool::restartCommandBuffer(int id) {

		VkCommandBuffer previousCommandBuffer = *m_commandBuffers.get(id);
		restartCommandBuffer(previousCommandBuffer);
		return this;
	}
	/**
	Calls restartCommandBuffer on all the stored command buffers.
	*/
	vktCommandPool* vktCommandPool::restartAll() {
		for (const auto& cmd : std::views::values(m_commandBuffers.get())) {
			restartCommandBuffer(cmd);
		}
		return this;
	}


	/**
	Ends the commandBuffer, then submits it (top of pipe).
	*/
	void vktCommandPool::submitQueueSingle(VkCommandBuffer commandBuffer, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore) {

		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));

		VkSubmitInfo submitInfo{};
		submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

		std::array<VkSemaphore, 1> waitSemaphores = { waitSemaphore };
		std::array<VkPipelineStageFlags, 1> waitStages = { VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT };
		if (waitSemaphore) {
			submitInfo.waitSemaphoreCount = 1;
			submitInfo.pWaitSemaphores = waitSemaphores.data();
			submitInfo.pWaitDstStageMask = waitStages.data();
		}

		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;

		std::array<VkSemaphore, 1> signalSemaphores = { signalSemaphore };
		if (signalSemaphore) {
			submitInfo.signalSemaphoreCount = 1;
			submitInfo.pSignalSemaphores = signalSemaphores.data();
		}

		if (vkQueueSubmit(vktQueue->queue, 1, &submitInfo, fence) != VK_SUCCESS) {
			throw std::runtime_error("failed to submit draw command buffer!");
		}
	}

	/**
	Submits the command buffer specified by id.
	@param id
	*/
	vktCommandPool* vktCommandPool::submit(const int id, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore) {

		VkCommandBuffer cmd = get(id);
		submitQueueSingle(cmd, fence, signalSemaphore, waitSemaphore);

		return this;
	}
	/**
	Submits the command buffer provided.
	*/
	vktCommandPool* vktCommandPool::submit(VkCommandBuffer cmd, VkFence fence, VkSemaphore signalSemaphore, VkSemaphore waitSemaphore) {

		submitQueueSingle(cmd, fence, signalSemaphore, waitSemaphore);

		return this;
	}

}