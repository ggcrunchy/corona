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

// ----------------------------------------------------------------------------

class RenderPassBuilder {
public:
	struct AttachmentOptions {
		VkClearValue * clear = NULL;
		VkSampleCountFlags samples = VK_SAMPLE_COUNT_1_BIT;
		bool noClear = false;
		bool isResolve = false;
	};

	void AddColorAttachment( VkFormat format, const AttachmentOptions & options = AttachmentOptions() );
	void AddDepthStencilAttachment( VkFormat format, const AttachmentOptions & options = AttachmentOptions() );
	void AddSubpassDependency( const VkSubpassDependency & dependency );

	VkRenderPass BuildForSingleSubpass();

private:
	std::vector< VkSubpassDependency > fDependencies;
	std::vector< VkAttachmentDescription > fDescriptions;
	std::vector< VkAttachmentReference > fReferences;
};

class VulkanFrameBufferObject : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanFrameBufferObject Self;

	public:
		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		virtual void Bind();

	private:

	private:
		VkImage fImage;
		VkImageView fView;
		VkFramebuffer fFramebuffer;
		VkRenderPass fRenderPass;
		U32 fIndex;
		bool fOwnsImages;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanFrameBufferObject_H__
