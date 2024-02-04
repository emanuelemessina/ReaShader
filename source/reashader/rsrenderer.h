/******************************************************************************
 * Copyright (c) Emanuele Messina (https://github.com/emanuelemessina)
 * All rights reserved.
 *
 * This code is licensed under the MIT License.
 * See the LICENSE file (https://github.com/emanuelemessina/ReaShader/blob/main/LICENSE) for more information.
 *****************************************************************************/

#pragma once

#include "tools/fwd_decl.h"

#include "vkt/vktcommon.h"
#include "vkt/vktdescriptors.h"
#include "vkt/vktdevices.h"
#include "vkt/vktimages.h"
#include "vkt/vktrendering.h"

namespace ReaShader
{
	FWD_DECL(ReaShaderProcessor)

class ReaShaderRenderer
{
  protected:
    ReaShaderProcessor *reaShaderProcessor;

    // must be greater than 0
    uint32_t FRAME_W{1}, FRAME_H{1};

  public:
    ReaShaderRenderer(ReaShaderProcessor *reaShaderProcessor);

    void init();
    void shutdown();

    void changeRenderingDevice(int renderingDeviceIndex);

    // public functions that drive the renderer, asynchronously called
    // make sure to invalidate the device if there's a device change in progress
    void checkFrameSize(int &w, int &h, void (*listener)() = nullptr);
    void loadBitsToImage(int *srcBuffer);
    // called inside drawFrame to update the general scene parameters to pass to the shaders
	void updateVirtualScene(double pushConstants[]);
	void drawFrame(double pushConstants[]);
    void transferFrame(int *&destBuffer);

  private:
    bool exceptionOnInitialize{false};
    bool halted{false};

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
    vkt::deletion_queue vktMainDeletionQueue{};
    vkt::deletion_queue vktFrameResizedDeletionQueue{};
    vkt::deletion_queue vktPhysicalDeviceChangedDeletionQueue{};

    vkt::Physical::Device *vktPhysicalDevice;
    vkt::Logical::Device *vktDevice;

    vkt::Images::AllocatedImage *vktFrameTransfer;
    vkt::Images::AllocatedImage *vktPostProcessSource;
    vkt::Images::AllocatedImage *vktColorAttachment;
    vkt::Images::AllocatedImage *vktDepthAttachment;

    VkRenderPass vkRenderPass;
    VkFramebuffer vkFramebuffer;

    VkCommandBuffer vkDrawCommandBuffer;
    VkCommandBuffer vkTransferCommandBuffer;

    VkSemaphore vkImageAvailableSemaphore;
    VkSemaphore vkRenderFinishedSemaphore;
    VkFence vkInFlightFence;

    vkt::vectors::searchable_map<int, vkt::Rendering::Mesh *> meshes;
    vkt::vectors::searchable_map<int, vkt::Rendering::Material> materials;
    vkt::vectors::searchable_map<int, vkt::Images::AllocatedImage *> textures;

    std::vector<vkt::Rendering::RenderObject> renderObjects;

    vkt::Descriptors::DescriptorPool *vktDescriptorPool;

    VkSampler vkSampler;

    struct VirtualSceneData
    {
        vkt::Buffers::AllocatedBuffer *cameraBuffer;
        vkt::Buffers::AllocatedBuffer *sceneBuffer;
        vkt::Buffers::AllocatedBuffer *objectBuffer;

        vkt::Descriptors::DescriptorSet globalSet;
        vkt::Descriptors::DescriptorSet objectSet;
        vkt::Descriptors::DescriptorSet textureSet;
    } virtualSceneData{};

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
};
} // namespace ReaShader
