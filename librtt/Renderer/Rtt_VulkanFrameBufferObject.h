//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanFrameBufferObject_H__
#define _Rtt_VulkanFrameBufferObject_H__

#include "Renderer/Rtt_GPUResource.h"

#include <vector>
#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class VulkanState;
class RenderPassKey;

// ----------------------------------------------------------------------------

class RenderPassBuilder {
	public:
		struct AttachmentOptions {
			VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
			bool noClear = false;
			bool isResolve = false;
		};

		void AddColorAttachment( VkFormat format, const AttachmentOptions & options = AttachmentOptions() );
		void AddDepthStencilAttachment( VkFormat format, const AttachmentOptions & options = AttachmentOptions() );
		void AddSubpassDependency( const VkSubpassDependency & dependency );

		VkRenderPass BuildForSingleSubpass( VkDevice device, const VkAllocationCallbacks * allocator ) const;
		void SingleSubpassKey( RenderPassKey & key ) const;

	private:
		void AddAttachment( VkAttachmentDescription description, std::vector< VkAttachmentReference > & references, VkImageLayout layout, bool isFinalLayout = true );

		std::vector< VkSubpassDependency > fDependencies;
		std::vector< VkAttachmentDescription > fDescriptions;
		std::vector< VkAttachmentReference > fColorReferences;
		std::vector< VkAttachmentReference > fDepthStencilReferences;
		std::vector< VkAttachmentReference > fResolveReferences;
};

class VulkanFrameBufferObject : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanFrameBufferObject Self;

	public:
		VulkanFrameBufferObject( VulkanState * state, uint32_t imageCount, VkImage * swapchainImages = NULL );

	public:
		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		virtual void Bind();

	private:
		struct FramebufferData {
			FramebufferData()
			:	fFramebuffer( VK_NULL_HANDLE )
			{
			}

			std::vector< VkImageView > fViews;
			VkFramebuffer fFramebuffer;
		};

		void MakeFramebuffers( uint32_t width, uint32_t height );

	private:
		VulkanState * fState;
		VkImage fImage;
		std::vector< FramebufferData > fFramebufferData;
		U32 fIndex;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanFrameBufferObject_H__
