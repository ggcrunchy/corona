//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanRenderer.h"

#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanCommandBuffer.h"
#include "Renderer/Rtt_VulkanFrameBufferObject.h"
#include "Renderer/Rtt_VulkanGeometry.h"
#include "Renderer/Rtt_VulkanProgram.h"
#include "Renderer/Rtt_VulkanTexture.h"
#include "Renderer/Rtt_CPUResource.h"
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanRenderer::VulkanRenderer( Rtt_Allocator* allocator, VulkanState * state )
:   Super( allocator ),
	fState( state ),
	fFirstPipeline( VK_NULL_HANDLE )
{
	fFrontCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator, *this ) );
	fBackCommandBuffer = Rtt_NEW( allocator, VulkanCommandBuffer( allocator, *this ) );

	InitializePipelineState();
}

VulkanRenderer::~VulkanRenderer()
{
	TearDownSwapchain();

    // vkDestroyCommandPool( fDevice, fCommandPool, fAllocator );

	Rtt_DELETE( fState );
}

void
VulkanRenderer::BuildUpSwapchain()
{
	VkSwapchainCreateInfoKHR swapchainCreateInfo = {};

	swapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainCreateInfo.imageArrayLayers = 1U;
	swapchainCreateInfo.imageColorSpace = fState->fSwapchainFormat.colorSpace;
	swapchainCreateInfo.imageExtent = fState->fSwapchainExtent;
	swapchainCreateInfo.imageFormat = fState->fSwapchainFormat.format;
	swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	swapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainCreateInfo.minImageCount = fState->fMaxSwapImageCount;

	if (fState->fQueueFamilies.size() > 1U)
	{
		swapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		swapchainCreateInfo.pQueueFamilyIndices = fState->fQueueFamilies.data();
		swapchainCreateInfo.queueFamilyIndexCount = 2U;
	}

	swapchainCreateInfo.clipped = VK_TRUE;
	swapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainCreateInfo.oldSwapchain = VK_NULL_HANDLE; // TODO!
	swapchainCreateInfo.presentMode = fState->fPresentMode;
	swapchainCreateInfo.preTransform = fState->fTransformFlagBits; // TODO: relevant to portrait, landscape, etc?
	swapchainCreateInfo.surface = fState->fSurface;

    const VkAllocationCallbacks * allocator = fState->GetAllocator();
    VkDevice device = fState->GetDevice();
	VkSwapchainKHR swapchain;

	if (VK_SUCCESS == vkCreateSwapchainKHR( device, &swapchainCreateInfo, allocator, &swapchain ))
	{
		fState->fSwapchain = swapchain;

		uint32_t imageCount = 0U;

		vkGetSwapchainImagesKHR( device, swapchain, &imageCount, NULL );

		std::vector< VkImage > images( imageCount );

		vkGetSwapchainImagesKHR( device, swapchain, &imageCount, images.data() );
		
		for ( const VkImage & image : images )
		{
			VkImageView view = VulkanTexture::CreateImageView( fState, image, fState->fSwapchainFormat.format, VK_IMAGE_ASPECT_COLOR_BIT, 1U );

			if (view != VK_NULL_HANDLE)
			{
				PerImageData pid;
				
				pid.image = image;
				pid.view = view;

				fPerImageData.push_back( pid );
			}

			else
			{
				CoronaLog( "Failed to create image views!" );

				// TODO: Error
			}
		}
	}

	else
	{
		CoronaLog( "Failed to create swap chain!" );

		// TODO: error
	}
}

/*
    void createFramebuffers() {
        swapChainFramebuffers.resize(swapChainImageViews.size());

        for (size_t i = 0; i < swapChainImageViews.size(); i++) {
            std::array<VkImageView, 3> attachments = {
                colorImageView,
                depthImageView,
                swapChainImageViews[i]
            };

            VkFramebufferCreateInfo framebufferInfo = {};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = swapChainExtent.width;
            framebufferInfo.height = swapChainExtent.height;
            framebufferInfo.layers = 1U;

            if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &swapChainFramebuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create framebuffer!");
            }
        }
    }
*/

void
VulkanRenderer::TearDownSwapchain()
{
    const VkAllocationCallbacks * allocator = fState->GetAllocator();
    VkDevice device = fState->GetDevice();

	for (PerImageData & data : fPerImageData)
	{
		vkDestroyImageView( device, data.view, allocator );
        // TODO: frame buffer

        data.image = VK_NULL_HANDLE;
        data.view = VK_NULL_HANDLE;
	}

//	fPerImageData.clear();
    // ^^ any good doing this? some of this WON'T need rebuilding...

	vkDestroySwapchainKHR( device, fState->fSwapchain, allocator );
}

GPUResource* 
VulkanRenderer::Create( const CPUResource* resource )
{
	switch( resource->GetType() )
	{
		case CPUResource::kFrameBufferObject: return new VulkanFrameBufferObject;
		case CPUResource::kGeometry: return new VulkanGeometry( fState );
		case CPUResource::kProgram: return new VulkanProgram( fState );
		case CPUResource::kTexture: return new VulkanTexture( fState );
		case CPUResource::kUniform: return NULL;
		default: Rtt_ASSERT_NOT_REACHED(); return NULL;
	}
}

static void
SetDynamicStateBit( uint8_t states[], uint8_t value )
{
	uint8_t offset = value;
	uint8_t byteIndex = offset / 8U;

	states[byteIndex] |= 1U << (offset - byteIndex * 8U);
}

void
VulkanRenderer::InitializePipelineState()
{
/*
	glDisable( GL_SCISSOR_TEST );
	// based on framebuffers, looks like we do NOT want this, but rather a full-screen scissor as default
*/
	PackedPipeline packedPipeline = {};

	// TODO: this should be fleshed out with defaults
	// these need not be relevant, but should properly handle being manually updated...

	packedPipeline.fRasterSamplesFlags = 1U;
	packedPipeline.fBlendAttachmentCount = 1U;
	packedPipeline.fBlendAttachments[0].fEnable = VK_TRUE;
	packedPipeline.fBlendAttachments[0].fColorWriteMask = 0xF;
	packedPipeline.fBlendAttachments[0].fSrcColorFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fSrcAlphaFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fDstColorFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	packedPipeline.fBlendAttachments[0].fDstAlphaFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;

	SetDynamicStateBit( packedPipeline.fDynamicStates, VK_DYNAMIC_STATE_SCISSOR );
	SetDynamicStateBit( packedPipeline.fDynamicStates, VK_DYNAMIC_STATE_VIEWPORT );

	fDefaultPipeline = packedPipeline;

	RestartWorkingPipeline();
}

void
VulkanRenderer::RestartWorkingPipeline()
{
	fShaderStageCreateInfo.clear();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
        
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;

	fInputAssemblyStateCreateInfo = inputAssembly;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};

	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.lineWidth = 1.0f;

	fRasterizationStateCreateInfo = rasterizer;

	VkPipelineMultisampleStateCreateInfo multisampling = {};

	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    fMultisampleStateCreateInfo = multisampling;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};

	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;

	fDepthStencilStateCreateInfo = depthStencil;

	VkPipelineColorBlendAttachmentState colorBlendAttachment = {};

	colorBlendAttachment.srcAlphaBlendFactor = colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
	colorBlendAttachment.dstAlphaBlendFactor = colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	fColorBlendAttachments.clear();
	fColorBlendAttachments.push_back( colorBlendAttachment );

	VkPipelineColorBlendStateCreateInfo colorBlending = {};

	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1U;
	fColorBlendStateCreateInfo = colorBlending;
}

void
VulkanRenderer::ResolvePipeline( VkCommandBuffer commandBuffer )
{
	auto iter = fBuiltPipelines.find( fWorkingPipeline );
	VkPipeline pipeline = VK_NULL_HANDLE;

	if (iter == fBuiltPipelines.end())
	{
	/*
        VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = 1;
        pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create pipeline layout!");
        }
	*/
		VkPipelineVertexInputStateCreateInfo vertexInputInfo = {};

        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		
        vertexInputInfo.pVertexAttributeDescriptions = fVertexAttributeDescriptions.data();
        vertexInputInfo.pVertexBindingDescriptions = fVertexBindingDescriptions.data();
        vertexInputInfo.vertexAttributeDescriptionCount = fVertexAttributeDescriptions.size();
        vertexInputInfo.vertexBindingDescriptionCount = fVertexBindingDescriptions.size();

		VkPipelineViewportStateCreateInfo viewportInfo = {};

		viewportInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;

        VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};

        pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipelineCreateInfo.basePipelineHandle = fFirstPipeline;
		pipelineCreateInfo.flags = fFirstPipeline != VK_NULL_HANDLE ? VK_PIPELINE_CREATE_DERIVATIVE_BIT : VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;
        pipelineCreateInfo.pInputAssemblyState = &fInputAssemblyStateCreateInfo;
        pipelineCreateInfo.pColorBlendState = &fColorBlendStateCreateInfo;
        pipelineCreateInfo.pDepthStencilState = &fDepthStencilStateCreateInfo;
        pipelineCreateInfo.pMultisampleState = &fMultisampleStateCreateInfo;
        pipelineCreateInfo.pRasterizationState = &fRasterizationStateCreateInfo;
        pipelineCreateInfo.pStages = fShaderStageCreateInfo.data();
        pipelineCreateInfo.stageCount = fShaderStageCreateInfo.size();
        pipelineCreateInfo.pVertexInputState = &vertexInputInfo;
		pipelineCreateInfo.pViewportState = &viewportInfo;
//      pipelineCreateInfo.layout = pipelineLayout;
//      pipelineCreateInfo.renderPass = renderPass;

		const VkAllocationCallbacks * allocator = fState->GetAllocator();

        if (VK_SUCCESS == vkCreateGraphicsPipelines( fState->GetDevice(), fState->GetPipelineCache(), 1U, &pipelineCreateInfo, allocator, &pipeline ))
		{
			if (VK_NULL_HANDLE == fFirstPipeline)
			{
				fFirstPipeline = pipeline;
			}

			fBuiltPipelines[fWorkingPipeline] = pipeline;
		}

		else
		{
			CoronaLog( "Failed to create pipeline!" );
        }
	}

	else
	{
		pipeline = iter->second;
	}

	if (pipeline != VK_NULL_HANDLE && (VK_NULL_HANDLE == fBoundPipeline || pipeline != fBoundPipeline))
	{
		vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}

	fBoundPipeline = pipeline;
	fWorkingPipeline = fDefaultPipeline;
}

bool
VulkanRenderer::PackedPipeline::operator < ( const PackedPipeline & other ) const
{
	return memcmp( this, &other, sizeof( PackedPipeline ) ) < 0;
}

bool
VulkanRenderer::PackedPipeline::operator == ( const PackedPipeline & other ) const
{
	return !(*this < other) && !(other < *this);
}

/*

    void createRenderPass() {
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = swapChainImageFormat;
        colorAttachment.samples = msaaSamples;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription depthAttachment = {};
        depthAttachment.format = findDepthFormat();
        depthAttachment.samples = msaaSamples;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentDescription colorAttachmentResolve = {};
        colorAttachmentResolve.format = swapChainImageFormat;
        colorAttachmentResolve.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachmentResolve.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachmentResolve.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachmentResolve.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachmentResolve.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachmentResolve.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef = {};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentResolveRef = {};
        colorAttachmentResolveRef.attachment = 2;
        colorAttachmentResolveRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass = {};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorAttachmentRef;
        subpass.pDepthStencilAttachment = &depthAttachmentRef;
        subpass.pResolveAttachments = &colorAttachmentResolveRef;

        VkSubpassDependency dependency = {};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription, 3> attachments = {colorAttachment, depthAttachment, colorAttachmentResolve };
        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
            throw std::runtime_error("failed to create render pass!");
        }
    }

    void createUniformBuffers() {
        VkDeviceSize bufferSize = sizeof(UniformBufferObject);

        uniformBuffers.resize(swapChainImages.size());
        uniformBuffersMemory.resize(swapChainImages.size());

        for (size_t i = 0; i < swapChainImages.size(); i++) {
            createBuffer(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, uniformBuffers[i], uniformBuffersMemory[i]);
        }
    }

    void createCommandBuffers() {
        commandBuffers.resize(swapChainFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t) commandBuffers.size();

        if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }

        for (size_t i = 0; i < commandBuffers.size(); i++) {
            VkCommandBufferBeginInfo beginInfo = {};
            beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            beginInfo.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;

            if (vkBeginCommandBuffer(commandBuffers[i], &beginInfo) != VK_SUCCESS) {
                throw std::runtime_error("failed to begin recording command buffer!");
            }

            VkRenderPassBeginInfo renderPassInfo = {};
            renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassInfo.renderPass = renderPass;
            renderPassInfo.framebuffer = swapChainFramebuffers[i];
            renderPassInfo.renderArea.offset = {0, 0};
            renderPassInfo.renderArea.extent = swapChainExtent;

            std::array<VkClearValue, 2> clearValues = {};
            clearValues[0].color = {0.0f, 0.0f, 0.0f, 1.0f};
            clearValues[1].depthStencil = {1.0f, 0};

            renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
            renderPassInfo.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(commandBuffers[i], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

                vkCmdBindPipeline(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

                VkBuffer vertexBuffers[] = {vertexBuffer};
                VkDeviceSize offsets[] = {0};
                vkCmdBindVertexBuffers(commandBuffers[i], 0, 1, vertexBuffers, offsets);

                vkCmdBindIndexBuffer(commandBuffers[i], indexBuffer, 0, VK_INDEX_TYPE_UINT32);

                vkCmdBindDescriptorSets(commandBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[i], 0, nullptr);

                vkCmdDrawIndexed(commandBuffers[i], static_cast<uint32_t>(indices.size()), 1, 0, 0, 0);

            vkCmdEndRenderPass(commandBuffers[i]);

            if (vkEndCommandBuffer(commandBuffers[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to record command buffer!");
            }
        }
    }

    void updateUniformBuffer(uint32_t currentImage) {
        static auto startTime = std::chrono::high_resolution_clock::now();

        auto currentTime = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

        UniformBufferObject ubo = {};
        ubo.model = glm::rotate(glm::mat4(1.0f), time * glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.view = glm::lookAt(glm::vec3(2.0f, 2.0f, 2.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
        ubo.proj = glm::perspective(glm::radians(45.0f), swapChainExtent.width / (float) swapChainExtent.height, 0.1f, 10.0f);
        ubo.proj[1][1] *= -1;

        void* data;
        vkMapMemory(device, uniformBuffersMemory[currentImage], 0, sizeof(ubo), 0, &data);
            memcpy(data, &ubo, sizeof(ubo));
        vkUnmapMemory(device, uniformBuffersMemory[currentImage]);
    }

    void drawFrame() {
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, std::numeric_limits<uint64_t>::max());

        uint32_t imageIndex;
        VkResult result = vkAcquireNextImageKHR(device, swapChain, std::numeric_limits<uint64_t>::max(), imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
            return;
        } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("failed to acquire swap chain image!");
        }

        updateUniformBuffer(imageIndex);

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

        VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
        VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;

        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffers[imageIndex];

        VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        vkResetFences(device, 1, &inFlightFences[currentFrame]);

        if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
            throw std::runtime_error("failed to submit draw command buffer!");
        }

        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;

        VkSwapchainKHR swapChains[] = {swapChain};
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapChains;

        presentInfo.pImageIndices = &imageIndex;

        result = vkQueuePresentKHR(presentQueue, &presentInfo);

        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapChain();
        } else if (result != VK_SUCCESS) {
            throw std::runtime_error("failed to present swap chain image!");
        }

        currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void createColorResources() {
        VkFormat colorFormat = swapChainImageFormat;

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory);
        colorImageView = createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        transitionImageLayout(colorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
    }

    void createDepthResources() {
        VkFormat depthFormat = findDepthFormat();

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
        depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
    }

    VkFormat findDepthFormat() {
        return findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }
*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------