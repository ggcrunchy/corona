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
class VulkanFrameBufferObject;
class VulkanState;

// ----------------------------------------------------------------------------

struct Descriptor {
	enum Index { kUniforms, kUserData, kTexture };

	Descriptor( VkDescriptorSetLayout setLayout );

	virtual void Reset( VkDevice device ) = 0;
	virtual void Wipe( VkDevice device, const VkAllocationCallbacks * allocator ) = 0;

	static bool IsMaskPushConstant( int index );
	static bool IsPushConstant( int index, bool userDataPushConstants );
	static bool IsUserData( int index );

	VkDescriptorSetLayout fSetLayout;
	U32 fDirty;
};

struct BufferData {
	BufferData();

	void Wipe();

	VkDescriptorSet fSet;
	VulkanBufferData * fData;
	void * fMapped;
};

struct BufferDescriptor : public Descriptor {
	BufferDescriptor( VulkanState * state, VkDescriptorPool pool, VkDescriptorSetLayout setLayout, VkDescriptorType type, size_t count, size_t size );

	void AllowMark() { fMarkWritten = true; }
	void ResetMark() { fWritten = 0U; }
	void TryToMark()
	{
		if (fMarkWritten)
		{
			fWritten |= fDirty;
		}
	}

	virtual void Reset( VkDevice device );
	virtual void Wipe( VkDevice device, const VkAllocationCallbacks * allocator );

	void SetWorkspace( void * workspace );
	void TryToAddMemory( std::vector< VkMappedMemoryRange > & ranges, VkDescriptorSet sets[], size_t & count );
	void TryToAddDynamicOffset( uint32_t offsets[], size_t & count );

	std::vector< BufferData > fBuffers;
	VkDescriptorSet fLastSet;
	VkDescriptorType fType;
	VkDeviceSize fDynamicAlignment;
	U8 * fWorkspace;
	U32 fIndex;
	U32 fOffset;
	U32 fLastOffset;
	size_t fAtomSize;
	size_t fBufferSize;
	size_t fRawSize;
	size_t fNonCoherentRawSize;
	U32 fWritten;
	bool fMarkWritten;
};

struct TexturesDescriptor : public Descriptor {
	TexturesDescriptor( VulkanState * state, VkDescriptorSetLayout setLayout );

	virtual void Reset( VkDevice device );
	virtual void Wipe( VkDevice device, const VkAllocationCallbacks * allocator );

	VkDescriptorPool fPool;
};

class VulkanRenderer : public Renderer
{
	public:
		typedef Renderer Super;
		typedef VulkanRenderer Self;

	public:
		VulkanRenderer( Rtt_Allocator* allocator, VulkanState * state );
		virtual ~VulkanRenderer();

		virtual void BeginFrame( Real totalTime, Real deltaTime, Real contentScaleX, Real contentScaleY, bool isCapture );
		virtual void EndFrame();
		virtual void CaptureFrameBuffer( RenderingStream & stream, BufferBitmap & bitmap, S32 x_in_pixels, S32 y_in_pixels, S32 w_in_pixels, S32 h_in_pixels );

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
		void SetMultisample( VkSampleCountFlagBits sampleCount );
		void SetPrimitiveTopology( VkPrimitiveTopology topology );
		void SetRenderPass( U32 id, VkRenderPass renderPass );
		void SetShaderStages( U32 id, const std::vector< VkPipelineShaderStageCreateInfo > & stages );

		VkPipeline ResolvePipeline();

		void ResetPipelineInfo();
		VkPipelineColorBlendAttachmentState & GetColorBlendState() { return fColorBlendState; }

	public:
		void PrepareCapture( VulkanFrameBufferObject * fbo, VkFence fence );

	protected:
		// Create an OpenGL resource appropriate for the given CPUResource.
		virtual GPUResource* Create( const CPUResource* resource );
		
	private:
		void InitializePipelineState();
		void RestartWorkingPipeline();
		void WipeDescriptors();

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
		VulkanFrameBufferObject * fCaptureFBO; // not owned by renderer
		std::vector< VkImage > fSwapchainImages;
		std::vector< VkCommandBuffer > fCommandBuffers;
		std::vector< Descriptor * > fDescriptors;
		std::map< PipelineKey, VkPipeline > fBuiltPipelines;
		VkPipeline fFirstPipeline;
		VkDescriptorPool fPool;
		VkDescriptorSetLayout fUniformsLayout;
		VkDescriptorSetLayout fUserDataLayout;
		VkDescriptorSetLayout fTextureLayout;
		VkPipelineLayout fPipelineLayout;
		VkFence fCaptureFence; // not owned by renderer
		PipelineCreateInfo fPipelineCreateInfo;
		PipelineKey fDefaultKey;
		PipelineKey fWorkingKey;
		VkPipelineColorBlendAttachmentState fColorBlendState;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanRenderer_H__
