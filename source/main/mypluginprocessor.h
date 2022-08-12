//------------------------------------------------------------------------
// Copyright(c) 2022 Emanuele Messina.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

#include "myplugincids.h"
#include <map>

#include "video_processor.h"

#include "mptorpdata/mptorpdata.h"

#include "pluginterfaces/vst/ivstmessage.h"

#include <vulkan/vulkan.h>
#include "vktools/vktools.h"
#include "vktools/vktdevices.h"
#include "vktools/vktimages.h"
#include "vktools/vktrendering.h"

#include <unordered_map>

using namespace Steinberg;
using namespace Vst;

namespace ReaShader {

	// internal processor params map
	typedef std::map<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue> rParamsMap;

	// reaper video processor functions
	IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms, double project_time, double frate, int force_format);
	bool getVideoParam(IREAPERVideoProcessor* vproc, int idx, double* valueOut);

	//------------------------------------------------------------------------
	//  ReaShaderProcessor
	//------------------------------------------------------------------------
	class ReaShaderProcessor : public Steinberg::Vst::AudioEffect
	{
	public:
		ReaShaderProcessor();
		~ReaShaderProcessor() SMTG_OVERRIDE;

		// Create function
		static Steinberg::FUnknown* createInstance(void* /*context*/)
		{
			return (Steinberg::Vst::IAudioProcessor*)new ReaShaderProcessor;
		}

		//--- ---------------------------------------------------------------------
		// AudioEffect overrides:
		//--- ---------------------------------------------------------------------
		/** Called at first after constructor */
		Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;

		/** Called at the end before destructor */
		Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;

		/** Switch the Plug-in on/off */
		Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;

		/** Will be called before any process call */
		Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;

		/** Asks if a given sample size is supported see SymbolicSampleSizes. */
		Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;

		/** Here we go...the process call */
		Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

		/** For persistence */
		Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
		Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

		//------------------------------------------------------------------------

		friend class ReaShaderController;
		friend IVideoFrame* processVideoFrame(IREAPERVideoProcessor* vproc, const double* parmlist, int nparms, double project_time, double frate, int force_format);

		/** We want to receive message. */
		Steinberg::tresult PLUGIN_API notify(Vst::IMessage* message) SMTG_OVERRIDE;
		/** Test of a communication channel between controller and component */
		tresult receiveText(const char* text) SMTG_OVERRIDE;

		FUnknown* context;
		IREAPERVideoProcessor* m_videoproc;

		rParamsMap rParams;
		RSData* m_data;

		int myColor;

		void initVulkan();
		void cleanupVulkan();

		VkInstance myVkInstance;
		vkt::vktDeletionQueue deletionQueue;
		vkt::vktInitProperties vktInitProperties{};

		vkt::vktPhysicalDevice* vktPhysicalDevice;
		vkt::vktDevice* vktDevice;

		vkt::AllocatedImage* vkFrameTransfer;
		vkt::AllocatedImage* vkColorAttachment;
		vkt::AllocatedImage* vkDepthAttachment;

		VkRenderPass vkRenderPass;
		VkFramebuffer vkFramebuffer;

		VkCommandBuffer vkDrawCommandBuffer;
		VkCommandBuffer vkTransferCommandBuffer;

		VkSemaphore vkImageAvailableSemaphore;
		VkSemaphore vkRenderFinishedSemaphore;
		VkFence vkInFlightFence;

		vkt::searchable_map<vkt::Mesh*> meshes;
		vkt::searchable_map<vkt::Material> materials;
		std::vector<vkt::RenderObject> renderObjects;

		VkDescriptorSetLayout vkObjectSetLayout;
		VkDescriptorSetLayout vkGlobalSetLayout;
		VkDescriptorSetLayout vkTextureSetLayout;

		VkDescriptorPool vkDescriptorPool;

		vkt::searchable_map<vkt::AllocatedImage*> textures;

		struct FrameData {

			vkt::AllocatedBuffer cameraBuffer;
			vkt::AllocatedBuffer sceneBuffer;
			vkt::AllocatedBuffer objectBuffer;

			VkDescriptorSet globalDescriptor;
			VkDescriptorSet objectDescriptor;
		} frameData;
	};

} // namespace ReaShader
