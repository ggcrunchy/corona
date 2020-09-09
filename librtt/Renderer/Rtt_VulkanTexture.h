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
#include "Renderer/Rtt_Texture.h"
#include <vulkan/vulkan.h>
#include <utility>

// ----------------------------------------------------------------------------

namespace Rtt
{

class VulkanBufferData;
class VulkanState;
struct DescriptorLists;

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

		void Bind( DescriptorLists & lists, VkDescriptorImageInfo & imageInfo );

	public:
		void CopyBufferToImage( VkBuffer buffer, VkImage image, uint32_t width, uint32_t height );
		bool Load( Texture * texture, VkFormat format, const VulkanBufferData & bufferData, U32 mipLevels );
		static bool TransitionImageLayout( VulkanState * state, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels );

		VkImage GetImage() const { return fData.fImage; }
		VkImageView GetImageView() const { return fImageView; }
		VkSampler GetSampler() const { return fSampler; }

	public:
		struct ImageData {
			ImageData()
			:	fImage( VK_NULL_HANDLE ),
				fMemory( VK_NULL_HANDLE )
			{
			}

			VkImage fImage;
			VkDeviceMemory fMemory;
		};

		static ImageData CreateImage( VulkanState * state, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties );
		static VkImageView CreateImageView( VulkanState * state, VkImage image, VkFormat format, VkImageAspectFlags flags, uint32_t mipLevels, const VkComponentMapping * componentMapping = NULL );
		static VkFormat GetVulkanFormat( Texture::Format format, VkComponentMapping & mapping );

	private:
		VulkanState * fState;
		ImageData fData;
		VkImageView fImageView;
		VkSampler fSampler;
//		uint32_t fMipLevels;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanTexture_H__
