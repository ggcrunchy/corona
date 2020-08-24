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
class FrameBufferObject;
class Texture;
class VulkanBufferData;
class VulkanState;

// ----------------------------------------------------------------------------

struct DynamicUniformData {
	DynamicUniformData();
	~DynamicUniformData();

	VulkanBufferData * fBufferData;
	void * fMapped;
};

struct DescriptorLists {
	enum ListIndex { kUniforms, kUserData, kTexture };

	DescriptorLists( VulkanState * state, VkDescriptorSetLayout setLayout, U32 count, size_t size );
	DescriptorLists( VkDescriptorSetLayout setLayout, bool resetPools = false );

	bool NoBuffers() const { return ~0U == fBufferIndex; }
	bool IsBufferFull() const { return fOffset == fBufferSize; }
	bool AddBuffer( VulkanState * state );
	bool AddPool( VulkanState * state, VkDescriptorType type, U32 descriptorCount, U32 maxSets, VkDescriptorPoolCreateFlags flags = 0 );
	bool EnsureAvailability( VulkanState * state );
	bool PreparePool( VulkanState * state );
	void Reset( VkDevice device, void * workspace = NULL );

	static bool IsMaskPushConstant( int index );
	static bool IsPushConstant( int index );
	static bool IsUserData( int index );

	std::vector< VkDescriptorSet > fSets;
	std::vector< VkDescriptorPool > fPools;
	std::vector< DynamicUniformData > fDynamicUniforms; // in normal scenarios, we should only ever use one of these...
	VkDescriptorSetLayout fSetLayout;
	VkDeviceSize fDynamicAlignment;
	U32 fBufferIndex; // ...i.e. index 0
	U32 fBufferSize;
	U32 fOffset;
	U32 fUpdateCount;
	U8 * fWorkspace;
	size_t fRawSize;
	bool fDirty;
	bool fResetPools;
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
		virtual void EndFrame();

	public:
		VkSwapchainKHR MakeSwapchain();

		void BuildUpSwapchain( VkSwapchainKHR swapchain );
		void RecreateSwapchain();
		void TearDownSwapchain();

	public:
		VulkanState * GetState() const { return fState; }
		VkDescriptorSetLayout GetUniformsLayout() const { return fUniformsLayout; }
		VkDescriptorSetLayout GetUserDataLayout() const { return fUserDataLayout; }
		VkDescriptorSetLayout GetTextureLayout() const { return fTextureLayout; }
		VkPipelineLayout GetPipelineLayout() const { return fPipelineLayout; }

		const std::vector< VkImage > & GetSwapchainImages() const { return fSwapchainImages; }

	public:
		void EnableBlend( bool enabled );
		void SetAttributeDescriptions( U32 id, const std::vector< VkVertexInputAttributeDescription > & descriptions );
		void SetBindingDescriptions( U32 id, const std::vector< VkVertexInputBindingDescription > & descriptions );
		void SetBlendEquations( VkBlendOp color, VkBlendOp alpha );
		void SetBlendFactors( VkBlendFactor srcColor, VkBlendFactor srcAlpha, VkBlendFactor dstColor, VkBlendFactor dstAlpha );
		void SetPrimitiveTopology( VkPrimitiveTopology topology, bool resolvePipeline = true );
		void SetRenderPass( U32 id, VkRenderPass renderPass );
		void SetShaderStages( U32 id, const std::vector< VkPipelineShaderStageCreateInfo > & stages );

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

			std::vector< VkPipelineColorBlendAttachmentState > fColorBlendAttachments;
			std::vector< VkPipelineShaderStageCreateInfo > fShaderStages;
			std::vector< VkVertexInputAttributeDescription > fVertexAttributeDescriptions;
			std::vector< VkVertexInputBindingDescription > fVertexBindingDescriptions;
			VkPipelineInputAssemblyStateCreateInfo fInputAssembly;
			VkPipelineRasterizationStateCreateInfo fRasterization;
			VkPipelineMultisampleStateCreateInfo fMultisample;
			VkPipelineDepthStencilStateCreateInfo fDepthStencil;
			VkPipelineColorBlendStateCreateInfo fColorBlend;
			VkRenderPass fRenderPass;
		};

		struct PipelineKey {
			PipelineKey();

			std::vector< U64 > fContents;

			bool operator < ( const PipelineKey & other ) const;
			bool operator == ( const PipelineKey & other ) const;
		};

		VulkanState * fState;
		Texture * fSwapchainTexture;
		FrameBufferObject * fPrimaryFBO;
		std::vector< VkImage > fSwapchainImages;
		std::vector< VkCommandBuffer > fCommandBuffers;
		std::vector< DescriptorLists > fDescriptorLists;
		std::map< PipelineKey, VkPipeline > fBuiltPipelines;
		VkPipeline fFirstPipeline;
		VkPipeline fBoundPipeline;
		VkDescriptorSetLayout fUniformsLayout;
		VkDescriptorSetLayout fUserDataLayout;
		VkDescriptorSetLayout fTextureLayout;
		VkPipelineLayout fPipelineLayout;
		PipelineCreateInfo fPipelineCreateInfo;
		PipelineKey fDefaultKey;
		PipelineKey fWorkingKey;
/*
    VkImage colorImage;
    VkDeviceMemory colorImageMemory;
    VkImageView colorImageView;

    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
*/
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanRenderer_H__
