/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include <atomic>
#include <mutex>


#include "vkt/vktcommon.h"
#include "vkt/vktdescriptors.h"
#include "vkt/vktdevices.h"
#include "vkt/vktimages.h"
#include "vkt/vktrendering.h"
#include "vkt/vktpipeline.h"


namespace ReaShader
{
	class ReaShaderPlugin;

class ReaShaderRenderer
{
  protected:
    ReaShaderPlugin *reaShaderPlugin;

    // must be greater than 0
    uint32_t FRAME_W{1}, FRAME_H{1};

  public:
    ReaShaderRenderer(ReaShaderPlugin *reaShaderPlugin);

    void init();
    void shutdown();

    void changeRenderingDevice(int renderingDeviceIndex);
	// compiles the GLSL fragment shader and replaces the plugin's shader params with its reflected uniforms;
	// never throws, errors go to onError
	using StatusCallback = std::function<void(const std::string&)>;
	void changeCustomShader(const std::string& source, StatusCallback onStatus, StatusCallback onError,
							std::function<void()> onSuccess);

    // Entry point for REAPER's video thread: runs checkFrameSize/loadBitsToImage/drawFrame/transferFrame
    // as one unit. Never throws -- returns false (caller should pass the input frame through) if the
    // renderer isn't initialized, is busy (device change/shutdown in progress), or hit a Vulkan error.
    // A Vulkan error (e.g. VK_ERROR_DEVICE_LOST) disables rendering until the next init().
    bool renderFrame(int w, int h, int *inputBits, double pushConstants[], int *outputBits);

    // public functions that drive the renderer, asynchronously called
    // make sure to invalidate the device if there's a device change in progress
    void checkFrameSize(int &w, int &h, void (*listener)() = nullptr);
    void loadBitsToImage(int *srcBuffer);
    // called inside drawFrame to update the general scene parameters to pass to the shaders
	void updateVirtualScene(double pushConstants[]);
	void drawFrame(double pushConstants[]);
    void transferFrame(int *&destBuffer);

  private:
    void _changeCustomShader(const std::string& source, const StatusCallback& onStatus,
                             const StatusCallback& onError, const std::function<void()>& onSuccess);

    bool exceptionOnInitialize{false};
    std::atomic<bool> halted{false};
    std::atomic<bool> frameFailed{false};

    // held by renderFrame (try_lock, so REAPER's video thread never waits on it) and by anything
    // that creates/destroys GPU resources frames use (init, shutdown, device and shader changes)
    std::mutex frameMutex;

    // wrap low level faults and circumvent seh object unwinding

    void _initVulkanGuarded();
    void _initVulkan();
    void _cleanupVulkanGuarded();
    void _cleanupVulkan();

    //-----------------------------------------------

    void setUpDevice(int renderingDeviceIndex);
    void createRenderTargets();

    // create defalt resources
	void _createDefaultMeshes();
	void _createDefaultTextures();
	void _setupRendering();

    std::vector<VkPhysicalDevice> vkSuitablePhysicalDevices;

    VkInstance myVkInstance;

    struct DeletionQueues
	{
		vkt::deletion_queue vktMain{};
		vkt::deletion_queue vktFrameResized{};
		vkt::deletion_queue vktPhysicalDeviceChanged{};
		vkt::deletion_queue vktCustomShaderChanged{};
	} deletionQueues;
    
    vkt::Physical::Device *vktPhysicalDevice{ nullptr };
    vkt::Logical::Device *vktDevice{ nullptr };

	struct RenderTargets
	{
		vkt::Images::AllocatedImage* vktFrameTransfer;
		vkt::Images::AllocatedImage* vktPostProcessSource;
		vkt::Images::AllocatedImage* vktColorAttachment;
		vkt::Images::AllocatedImage* vktDepthAttachment;
    } renderTargets;
    

    VkRenderPass vkRenderPass;
    VkFramebuffer vkFramebuffer;

    struct CommandBuffers
	{
		VkCommandBuffer vkDraw;
		VkCommandBuffer vkTransfer;
    } commandBuffers;
    
    struct SyncObjects
	{
		VkSemaphore vkImageAvailableSemaphore;
		VkSemaphore vkRenderFinishedSemaphore;
		VkFence vkInFlightFence;
    } syncObjects;
    
    vkt::vectors::searchable_map<int, vkt::Rendering::Mesh *> meshes;
    vkt::vectors::searchable_map<int, vkt::Rendering::Material> materials;
    vkt::vectors::searchable_map<int, vkt::Images::AllocatedImage *> textures;

    std::vector<vkt::Rendering::RenderObject> renderObjects;

    vkt::Descriptors::DescriptorPool *vktDescriptorPool;

    VkSampler vkSampler;

    struct VirtualScene
    {
        vkt::Buffers::AllocatedBuffer *cameraBuffer;
        vkt::Buffers::AllocatedBuffer *environmentBuffer;
        vkt::Buffers::AllocatedBuffer *objectBuffer;

        vkt::Descriptors::DescriptorSet globalSet;
        vkt::Descriptors::DescriptorSet objectSet;
        vkt::Descriptors::DescriptorSet textureSet;

        struct VirtualCameraData
		{
			glm::mat4 view;
			glm::mat4 proj;
			glm::mat4 viewproj;
		} camData;

        struct VirtualEnvironmentData
		{
			glm::vec4 fogColor;		// w is for exponent
			glm::vec4 fogDistances; // x for min, y for max, zw unused.
			glm::vec4 ambientColor;
			glm::vec4 sunlightDirection; // w for sun power
			glm::vec4 sunlightColor;
		} envData;

    } virtualScene{};

    struct PostProcess
	{
		vkt::Descriptors::DescriptorSet globalSet;

        std::vector<vkt::Buffers::AllocatedBuffer*> buffers;
		std::vector<vkt::Images::AllocatedImage*> textures;

	} postProcessData;
};
} // namespace ReaShader
