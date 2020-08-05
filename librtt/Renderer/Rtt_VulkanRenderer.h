//////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2018 Corona Labs Inc.
// Contact: support@coronalabs.com
//
// This file is part of the Corona game engine.
//
// Commercial License Usage
// Licensees holding valid commercial Corona licenses may use this file in
// accordance with the commercial license agreement between you and 
// Corona Labs Inc. For licensing terms and conditions please contact
// support@coronalabs.com or visit https://coronalabs.com/com-license
//
// GNU General Public License Usage
// Alternatively, this file may be used under the terms of the GNU General
// Public license version 3. The license is as published by the Free Software
// Foundation and appearing in the file LICENSE.GPL3 included in the packaging
// of this file. Please review the following information to ensure the GNU 
// General Public License requirements will
// be met: https://www.gnu.org/licenses/gpl-3.0.html
//
// For overview and more information on licensing please refer to README.md
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanRenderer_H__
#define _Rtt_VulkanRenderer_H__

#include "Renderer/Rtt_Renderer.h"

#include <map>
#include <vector>

#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------

struct Rtt_Allocator;

namespace Rtt
{

class GPUResource;
class CPUResource;
class VulkanState;
class VulkanFrameBufferObject;

// ----------------------------------------------------------------------------

struct DescriptorPoolList {
	std::vector< VkDescriptorPool > fPools;
	uint32_t fIndex = 0U;
};

class VulkanRenderer : public Renderer
{
	public:
		typedef Renderer Super;
		typedef VulkanRenderer Self;

	public:
		VulkanRenderer( Rtt_Allocator* allocator, VulkanState * state );
		virtual ~VulkanRenderer();

		virtual void BeginFrame( Real totalTime, Real deltaTime, Real contentScaleX, Real contentScaleY );

	public:
		VkSwapchainKHR MakeSwapchain();

		void BuildUpSwapchain( VkSwapchainKHR swapchain );
		void RecreateSwapchain();
		void TearDownSwapchain();

	public:
		VulkanState * GetState() const { return fState; }

	public:
		void EnableBlend( bool enabled );
		void SetAttributeDescriptions( U32 id, const std::vector< VkVertexInputAttributeDescription > & descriptions );
		void SetBindingDescriptions( U32 id, const std::vector< VkVertexInputBindingDescription > & descriptions );
		void SetBlendEquations( VkBlendOp color, VkBlendOp alpha );
		void SetBlendFactors( VkBlendFactor srcColor, VkBlendFactor srcAlpha, VkBlendFactor dstColor, VkBlendFactor dstAlpha );
		void SetPrimitiveTopology( VkPrimitiveTopology topology, bool resolvePipeline = true );
		void SetShaderStages( U32 id, const std::vector< VkPipelineShaderStageCreateInfo > & stages );

	public:
		void SetClearValue( U32 index, const VkClearValue & clearValue );

	public:
		// cf. shell_default_vulkan:

		struct UniformObjects {
			typedef float Mat4[16];
			typedef float Vec4[4];

			struct UniformBuffer {
				alignas(16) Vec4 fVectors[6];
			};

			struct UserData {
				alignas(16) Mat4 UserData[4];
			};

			struct PushConstant {
				alignas(16) Vec4 fVectors[5];
			};
		};

	protected:
		// Create an OpenGL resource appropriate for the given CPUResource.
		virtual GPUResource* Create( const CPUResource* resource );
		
	private:
		void InitializePipelineState();
		void RestartWorkingPipeline();
		void ResolvePipeline();

	private:
		struct PipelineCreateInfo {
			PipelineCreateInfo();

			std::vector< VkDynamicState > fDynamicState;
			std::vector< VkPipelineColorBlendAttachmentState > fColorBlendAttachments;
			std::vector< VkPipelineShaderStageCreateInfo > fShaderStages;
			std::vector< VkVertexInputAttributeDescription > fVertexAttributeDescriptions;
			std::vector< VkVertexInputBindingDescription > fVertexBindingDescriptions;
VkRect2D fScissorRect;
VkViewport fViewport;
// ^^ TODO: do we need these, or just do immediate bind commands?

			VkPipelineInputAssemblyStateCreateInfo fInputAssembly;
			VkPipelineRasterizationStateCreateInfo fRasterization;
			VkPipelineMultisampleStateCreateInfo fMultisample;
			VkPipelineDepthStencilStateCreateInfo fDepthStencil;
			VkPipelineColorBlendStateCreateInfo fColorBlend;
		};

		struct PipelineKey {
			PipelineKey();

			std::vector< U64 > fContents;

			bool operator < ( const PipelineKey & other ) const;
			bool operator == ( const PipelineKey & other ) const;
		};

		VulkanState * fState;
		VulkanFrameBufferObject * fFBO;
		std::vector< VkClearValue > fClearValues;
		std::vector< VkImage > fSwapchainImages;
		std::vector< VkCommandBuffer > fCommandBuffers;
		std::vector< DescriptorPoolList > fDescriptorPools;
		std::map< PipelineKey, VkPipeline > fBuiltPipelines;
		VkPipeline fFirstPipeline;
		VkPipeline fBoundPipeline;
		VkPipelineLayout fPipelineLayout;
		PipelineCreateInfo fPipelineCreateInfo;
		PipelineKey fDefaultKey;
		PipelineKey fWorkingKey;
/*
    VkRenderPass renderPass;
    VkDescriptorSetLayout descriptorSetLayout;
    VkPipelineLayout pipelineLayout;
    VkPipeline graphicsPipeline;

    VkDescriptorPool descriptorPool;


    VkImage colorImage;
    VkDeviceMemory colorImageMemory;
    VkImageView colorImageView;

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
*/
};

/*
struct UniformBufferObject {
    alignas(16) glm::mat4 model;
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
};
*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanRenderer_H__
