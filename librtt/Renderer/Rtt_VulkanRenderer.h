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
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

constexpr int BitsNeeded( int x ) // n.b. x > 0
{
	int result = 0;

	for (int power = 1; power <= x; power *= 2)
	{
		++result;
	}

	return result;
}

// ----------------------------------------------------------------------------

struct Rtt_Allocator;

namespace Rtt
{

class GPUResource;
class CPUResource;
class VulkanState;

// ----------------------------------------------------------------------------

class VulkanRenderer : public Renderer
{
	public:
		typedef Renderer Super;
		typedef VulkanRenderer Self;

	public:
		VulkanRenderer( Rtt_Allocator* allocator, VulkanState * state );
		virtual ~VulkanRenderer();

	public:
		void BuildUpSwapchain();
		void TearDownSwapchain();

	protected:
		// Create an OpenGL resource appropriate for the given CPUResource.
		virtual GPUResource* Create( const CPUResource* resource );
		
	private:
		void InitializePipelineState();
		void RestartWorkingPipeline();
		void ResolvePipeline( VkCommandBuffer commandBuffer );

	private:
		struct Attachment {
			VkImage image;
			VkImageView view;
			VkDeviceMemory memory;
		};

		struct PerImageData {
			std::vector< Attachment > attachments;

			// uniform buffer stuff
			VkCommandPool fCommandPool;
			VkDescriptorPool fDescriptorPool;
			VkImage image;
			VkImageView view;
			VkFramebuffer framebuffer;
		};

	private:
		enum {
			kFinalBlendFactor = VK_BLEND_OP_MAX,
			kFinalBlendOp = VK_BLEND_OP_MAX,
			kFinalCompareOp = VK_COMPARE_OP_ALWAYS,
			kFinalDynamicState = VK_DYNAMIC_STATE_STENCIL_REFERENCE,
			kFinalFrontFace = VK_FRONT_FACE_CLOCKWISE,
			kFinalLogicOp = VK_LOGIC_OP_SET,
			kFinalPolygonMode = VK_POLYGON_MODE_POINT,
			kFinalPrimitiveTopology = VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
			kFinalStencilOp = VK_STENCIL_OP_DECREMENT_AND_WRAP
		};

		struct PackedBlendAttachment {
			U32 fEnable : 1;
			U32 fSrcColorFactor : BitsNeeded( kFinalBlendFactor );
			U32 fDstColorFactor : BitsNeeded( kFinalBlendFactor );
			U32 fColorOp : BitsNeeded( kFinalBlendOp );
			U32 fSrcAlphaFactor : BitsNeeded( kFinalBlendFactor );
			U32 fDstAlphaFactor : BitsNeeded( kFinalBlendFactor );
			U32 fAlphaOp : BitsNeeded( kFinalBlendOp );
			U32 fColorWriteMask : 4;
		};

		struct PackedPipeline {
			enum {
				kDynamicStateCountRoundedUp = (kFinalDynamicState + 7U) & ~7U,
				kDynamicStateByteCount = kDynamicStateCountRoundedUp / 8U
			};

            U64 fTopology : BitsNeeded( kFinalPrimitiveTopology );
            U64 fPrimitiveRestartEnable : 1;
			U64 fRasterizerDiscardEnable : 1;
			U64 fPolygonMode : BitsNeeded( kFinalPolygonMode );
			U64 fLineWidth : 4; // lineWidth = (X + 1) / 16
			U64 fCullMode : 2;
			U64 fFrontFace : BitsNeeded( kFinalFrontFace );
			U64 fRasterSamplesFlags : 7;
			U64 fSampleShadingEnable : 1;
			U64 fSampleShading : 5; // minSampleShading = X / 32
			U64 fAlphaToCoverageEnable : 1;
			U64 fAlphaToOneEnable : 1;
			U64 fDepthTestEnable : 1;
			U64 fDepthWriteEnable : 1;
			U64 fDepthCompareOp : BitsNeeded( kFinalCompareOp );
			U64 fDepthBoundsTestEnable : 1;
			U64 fStencilTestEnable : 1;
			U64 fFront : BitsNeeded( kFinalStencilOp );
			U64 fBack : BitsNeeded( kFinalStencilOp );
			U64 fMinDepthBounds : 5; // minDepthBounds = X / 32
			U64 fMaxDepthBounds : 5; // maxDepthBounds = (X + 1) / 32
			U64 fLogicOpEnable : 1;
			U64 fLogicOp : BitsNeeded( kFinalLogicOp );
			U64 fBlendConstant1 : 4; // blendConstants = X / 15
			U64 fBlendConstant2 : 4;
			U64 fBlendConstant3 : 4;
			U64 fBlendConstant4 : 4;
			U64 fLayoutID : 4;
			U64 fShaderID : 16;
			U64 fAttributeDescriptionID : 3;
			U64 fBindingDescriptionID : 3;
			U64 fBlendAttachmentCount : 3; // 0-7
			PackedBlendAttachment fBlendAttachments[8];
			uint8_t fDynamicStates[kDynamicStateByteCount];

			bool operator < ( const PackedPipeline & other ) const;
			bool operator == ( const PackedPipeline & other ) const;
		};

		VulkanState * fState;
		std::vector< PerImageData > fPerImageData;
		std::map< PackedPipeline, VkPipeline > fBuiltPipelines;
		std::vector< VkDynamicState > fDynamicState;
		std::vector< VkPipelineShaderStageCreateInfo > fShaderStageCreateInfo;
		std::vector< VkPipelineColorBlendAttachmentState > fColorBlendAttachments;
		std::vector< VkVertexInputAttributeDescription > fVertexAttributeDescriptions;
        std::vector< VkVertexInputBindingDescription > fVertexBindingDescriptions;
VkRect2D fScissorRect;
VkViewport fViewport;
// ^^ TODO: do we need these, or just do immediate bind commands?
		VkPipeline fFirstPipeline;
		VkPipeline fBoundPipeline;
		PackedPipeline fDefaultPipeline;
		PackedPipeline fWorkingPipeline;
		VkPipelineInputAssemblyStateCreateInfo fInputAssemblyStateCreateInfo;
        VkPipelineRasterizationStateCreateInfo fRasterizationStateCreateInfo;
        VkPipelineMultisampleStateCreateInfo fMultisampleStateCreateInfo;
        VkPipelineDepthStencilStateCreateInfo fDepthStencilStateCreateInfo;
        VkPipelineColorBlendStateCreateInfo fColorBlendStateCreateInfo;

		friend class VulkanCommandBuffer;
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
