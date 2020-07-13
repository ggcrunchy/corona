//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_VulkanTexture_H__
#define _Rtt_VulkanTexture_H__

#include "Renderer/Rtt_GPUResource.h"
#include <vulkan/vulkan.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

class VulkanState;

// ----------------------------------------------------------------------------

class VulkanTexture : public GPUResource
{
	public:
		typedef GPUResource Super;
		typedef VulkanTexture Self;

	public:
		VulkanTexture( VulkanState * state );

	public:
		virtual void Create( CPUResource* resource );
		virtual void Update( CPUResource* resource );
		virtual void Destroy();
		virtual void Bind( U32 unit );

//		virtual GLuint GetName();
	private:
		/*
		GLint fCachedFormat;
		unsigned long fCachedWidth, fCachedHeight;*/
		VulkanState * fState;
		VkImage fImage;
		VkDeviceMemory fImageMemory;
		VkImageView fImageView;
		VkSampler fSampler;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanTexture_H__
