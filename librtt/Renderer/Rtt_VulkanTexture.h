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
#include <vector>

// ----------------------------------------------------------------------------

namespace Rtt
{

class VulkanBufferData;
class VulkanState;
struct Descriptor;

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

		void Bind( Descriptor & desc, VkDescriptorImageInfo & imageInfo );
		void Toggle();

	public:
		void CopyBufferToImage( VkBuffer buffer, VkImage image, uint32_t width, uint32_t height );
		bool Load( Texture * texture, VkFormat format, const VulkanBufferData & bufferData, U32 mipLevels );
		static bool TransitionImageLayout( VulkanState * state, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels, VkCommandBuffer = VK_NULL_HANDLE );

		VkImage GetImage() const { return fData[ GetIndex() ].fImage; }
		VkImageView GetImageView() const { return fData[ GetIndex() ].fView; }
		VkSampler GetSampler() const { return fSampler; }
		VkFormat GetFormat() const { return fFormat; }
		uint32_t GetIndex() const { return fToggled ? 1 : 0; }
		size_t GetImageCount() const { return fData.size(); }
		bool GetUpdated() const { return fUpdated;  }

		void SetUpdated( bool newValue ) { fUpdated = newValue; }

	public:
		struct ImageData {
			ImageData()
			:	fImage( VK_NULL_HANDLE ),
				fView( VK_NULL_HANDLE ),
				fMemory( VK_NULL_HANDLE )
			{
			}

			VkImage fImage;
			VkImageView fView;
			VkDeviceMemory fMemory;
		};

		static ImageData CreateImage( VulkanState * state, uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties );
		static VkImageView CreateImageView( VulkanState * state, VkImage image, VkFormat format, VkImageAspectFlags flags, uint32_t mipLevels, const VkComponentMapping * componentMapping = NULL );
		static VkFormat GetVulkanFormat( Texture::Format format, VkComponentMapping & mapping );

	private:
		std::vector< ImageData > fData;
		VulkanState * fState;
		VkSampler fSampler;
		VkFormat fFormat;
//		uint32_t fMipLevels;
		bool fToggled;
		bool fUpdated;
};

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_VulkanTexture_H__
