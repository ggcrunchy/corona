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
