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
#include <utility>

// ----------------------------------------------------------------------------

namespace Rtt
{

class VulkanBufferData;
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

	public:
		void CopyBufferToImage( VkBuffer buffer, VkImage image, uint32_t width, uint32_t height );
		void CreateImage( uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties );
		bool Load( Texture * texture, const VulkanBufferData & bufferData, U32 mipLevels );
		bool TransitionImageLayout( VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels );

	public:
		static VkImageView CreateImageView( VulkanState * state, VkImage image, VkFormat format, VkImageAspectFlags flags, uint32_t mipLevels, const VkComponentMapping * componentMapping = NULL );

	private:
//		GLint fCachedFormat;
//		unsigned long fCachedWidth, fCachedHeight;
		VulkanState * fState;
		VkDeviceMemory fImageMemory;
		VkImageView fImageView;
		VkImage fImage;
		VkSampler fSampler;
//		uint32_t fMipLevels;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanTexture_H__
