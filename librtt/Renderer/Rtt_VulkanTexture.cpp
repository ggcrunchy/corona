//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Config.h"

#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanTexture.h"
#include "Renderer/Rtt_Texture.h"
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"

#include <algorithm>
#include <cmath>

// ----------------------------------------------------------------------------
/*
#define ENABLE_DEBUG_PRINT	0

#if ENABLE_DEBUG_PRINT
	#define DEBUG_PRINT( ... ) Rtt_LogException( __VA_ARGS__ );
#else
	#define DEBUG_PRINT( ... )
#endif
	*/
// ----------------------------------------------------------------------------

namespace /*anonymous*/ 
{ 
	using namespace Rtt;

	VkFormat getFormatTokens( Texture::Format format, VkComponentMapping & mapping )
	{
        VkFormat vulkanFormat = VK_FORMAT_R8G8B8A8_SRGB;

		switch( format )
		{
			case Texture::kAlpha:
                vulkanFormat = VK_FORMAT_R8G8B8A8_SRGB;
                mapping.r = mapping.g = mapping.b = VK_COMPONENT_SWIZZLE_A;

                break;
            // ^^ TODO: guess!
			case Texture::kLuminance:
                vulkanFormat = VK_FORMAT_R8_SRGB;
                
                break;
            // ^^ TODO: guess!
            case Texture::kRGB:
                vulkanFormat = VK_FORMAT_R8G8B8_SRGB;
                
                break;
            case Texture::kRGBA:
                break;
			case Texture::kARGB:
                mapping.r = VK_COMPONENT_SWIZZLE_A;
                mapping.g = VK_COMPONENT_SWIZZLE_R;
                mapping.b = VK_COMPONENT_SWIZZLE_G;
                mapping.a = VK_COMPONENT_SWIZZLE_B;

                break;
			case Texture::kBGRA:
                mapping.r = VK_COMPONENT_SWIZZLE_B;
                mapping.g = VK_COMPONENT_SWIZZLE_G;
                mapping.b = VK_COMPONENT_SWIZZLE_R;

                break;
			case Texture::kABGR:
                mapping.r = VK_COMPONENT_SWIZZLE_A;
                mapping.g = VK_COMPONENT_SWIZZLE_B;
                mapping.b = VK_COMPONENT_SWIZZLE_G;
                mapping.a = VK_COMPONENT_SWIZZLE_R;

                break;
			default: Rtt_ASSERT_NOT_REACHED();
		}

        return vulkanFormat;
	}

	void getFilterTokens( Texture::Filter filter, VkFilter & minFilter, VkFilter & magFilter )
	{
		switch( filter )
		{
			case Texture::kNearest:	minFilter = VK_FILTER_NEAREST;	magFilter = VK_FILTER_NEAREST;	break;
			case Texture::kLinear:	minFilter = VK_FILTER_LINEAR;	magFilter = VK_FILTER_LINEAR;	break;
			default: Rtt_ASSERT_NOT_REACHED();
		}
	}

	VkSamplerAddressMode convertWrapToken( Texture::Wrap wrap )
	{
		VkSamplerAddressMode result = VK_SAMPLER_ADDRESS_MODE_REPEAT;

		switch( wrap )
		{
			case Texture::kClampToEdge:		result = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; break;
			case Texture::kRepeat:			result = VK_SAMPLER_ADDRESS_MODE_REPEAT; break;
			case Texture::kMirroredRepeat:	result = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT; break;
            // also available: clamp to border, mirrored clamp to edge
			default: Rtt_ASSERT_NOT_REACHED();
		}

		return result;
	}
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

VulkanTexture::VulkanTexture( VulkanState * state )
	:	fState( state )
{
}

void 
VulkanTexture::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kTexture == resource->GetType() || CPUResource::kVideoTexture == resource->GetType() );
	Texture* texture = static_cast<Texture*>( resource );

    VkComponentMapping mapping = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
    VkFormat format = getFormatTokens( texture->GetFormat(), mapping );
    
    VkDeviceSize imageSize = texture->GetSizeInBytes();
    U32 mipLevels = static_cast<uint32_t>( std::floor( std::log2( std::max( texture->GetWidth(), texture->GetHeight() ) ) ) ) + 1U;
    auto stagingData = fState->CreateBuffer( imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
    bool ok = stagingData.first != VK_NULL_HANDLE; // ^^ TODO: also non-buffered approach

    if (ok)
    {
        CreateImage(
            texture->GetWidth(), texture->GetHeight(),
            1U, // mip levels
            VK_SAMPLE_COUNT_1_BIT,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TILING_OPTIMAL, // might not want if frequently changed?
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

        ok = fImage != VK_NULL_HANDLE;

        if (ok)
        {
            ok = Load( texture, stagingData.first, stagingData.second, mipLevels );
        }
    }
    
    texture->ReleaseData();

    if (ok)
    {
        VkSamplerCreateInfo samplerInfo = {};

        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

        getFilterTokens( texture->GetFilter(), samplerInfo.minFilter, samplerInfo.magFilter );
     
        samplerInfo.addressModeU = convertWrapToken( texture->GetWrapX() );
        samplerInfo.addressModeV = convertWrapToken( texture->GetWrapY() );
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
/*
        samplerInfo.anisotropyEnable = VK_TRUE;
        samplerInfo.maxAnisotropy = 16;
*/
        samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
/*
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
*/
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.maxLod = 1U;

        VkSampler sampler;

        if (VK_SUCCESS == vkCreateSampler( fState->GetDevice(), &samplerInfo, fState->GetAllocationCallbacks(), &sampler))
        {
            fSampler = sampler;
            fImageView = CreateImageView( fState, fImage, format, VK_IMAGE_ASPECT_COLOR_BIT, mipLevels, &mapping );
        }
        
        else
        {
            CoronaLog( "Failed to create texture sampler!" );
        }
    }
}

void 
VulkanTexture::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kTexture == resource->GetType() );
	Texture* texture = static_cast<Texture*>( resource );
	/*
	const U8* data = texture->GetData();		
	if( data )
	{		
		const U32 w = texture->GetWidth();
		const U32 h = texture->GetHeight();
		GLint internalFormat;
		GLenum format;
		GLenum type;
		getFormatTokens( texture->GetFormat(), internalFormat, format, type );

		glBindTexture( GL_TEXTURE_2D, GetName() );
		if (internalFormat == fCachedFormat && w == fCachedWidth && h == fCachedHeight )
		{
			glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data );
		}
		else
		{
			glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data );
			fCachedFormat = internalFormat;
			fCachedWidth = w;
			fCachedHeight = h;
		}
		GL_CHECK_ERROR();
	}
	texture->ReleaseData();*/
}

void 
VulkanTexture::Destroy()
{/*
	GLuint name = GetName();
	if ( 0 != name )
	{
		glDeleteTextures( 1, &name );
		GL_CHECK_ERROR();
		fHandle = 0;	
	}

	DEBUG_PRINT( "%s : OpenGL name: %d\n",
					Rtt_FUNCTION,
					name );*/
}

void 
VulkanTexture::Bind( U32 unit )
{/*
	glActiveTexture( GL_TEXTURE0 + unit );
	glBindTexture( GL_TEXTURE_2D, GetName() );
	GL_CHECK_ERROR();*/
}

void
VulkanTexture::CopyBufferToImage( VkBuffer buffer, VkImage image, uint32_t width, uint32_t height )
{
    VkCommandBuffer commandBuffer = fState->BeginSingleTimeCommands();

    VkBufferImageCopy region = {};

    region.imageExtent = { width, height, 1U };
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;

    vkCmdCopyBufferToImage( commandBuffer, buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1U, &region );

    fState->EndSingleTimeCommands( commandBuffer );
}

void
VulkanTexture::CreateImage( uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits numSamples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties )
{
    VkImageCreateInfo createImageInfo = {};

    createImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    createImageInfo.arrayLayers = 1U;
    createImageInfo.imageType = VK_IMAGE_TYPE_2D;
    createImageInfo.extent = { width, height, 1U };
    createImageInfo.format = format;
    createImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    createImageInfo.mipLevels = mipLevels;
    createImageInfo.samples = numSamples;
    createImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createImageInfo.tiling = tiling;
    createImageInfo.usage = usage;

    const VkAllocationCallbacks * allocator = fState->GetAllocationCallbacks();
    VkDevice device = fState->GetDevice();

    if (VK_SUCCESS == vkCreateImage( device, &createImageInfo, allocator, &fImage ))
    {
        VkMemoryRequirements memRequirements;

        vkGetImageMemoryRequirements( device, fImage, &memRequirements );

        VkMemoryAllocateInfo allocInfo = {};

        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;

        bool foundMemoryType = fState->FindMemoryType( memRequirements.memoryTypeBits, properties, allocInfo.memoryTypeIndex );

        if (foundMemoryType && vkAllocateMemory( device, &allocInfo, allocator, &fImageMemory ) != VK_SUCCESS)
        {
            CoronaLog( "Failed to allocate image memory!" );
        }

        if (fImageMemory != VK_NULL_HANDLE)
        {
            vkBindImageMemory( device, fImage, fImageMemory, 0 );
        }

        else
        {
            vkDestroyImage( device, fImage, allocator );

            fImage = VK_NULL_HANDLE;
        }
    }

    else
    {
        CoronaLog( "Failed to create image!" );
    }
}

bool
VulkanTexture::Load( Texture * texture, VkBuffer buffer, VkDeviceMemory bufferMemory, U32 mipLevels )
{
    bool ok = false;

    if (buffer != VK_NULL_HANDLE)
    {
        fState->StageData( bufferMemory, texture->GetData(), texture->GetSizeInBytes() );
        
        ok = TransitionImageLayout( fImage, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels );

        if (ok)
        {
            CopyBufferToImage( buffer, fImage, texture->GetWidth(), texture->GetHeight() );
        }

        // transitioned to VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL while generating mipmaps
        // TODO: ^^ bring this in, then
    
        const VkAllocationCallbacks * allocator = fState->GetAllocationCallbacks();
        VkDevice device = fState->GetDevice();

        vkDestroyBuffer( device, buffer, allocator );
        vkFreeMemory( device, bufferMemory, allocator );

//  generateMipmaps(textureImage, VK_FORMAT_R8G8B8A8_UNORM, texWidth, texHeight, mipLevels);
    }

    return ok;
}

static bool
HasStencilComponent( VkFormat format )
{
    return VK_FORMAT_D32_SFLOAT_S8_UINT == format || VK_FORMAT_D24_UNORM_S8_UINT == format;
}

bool
VulkanTexture::TransitionImageLayout( VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels )
{
    VkCommandBuffer commandBuffer = fState->BeginSingleTimeCommands();
    VkImageMemoryBarrier barrier = {};

    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

    if (VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL == newLayout)
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;

        if (HasStencilComponent(format))
        {
            barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
        
    else
    {
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    }

    barrier.subresourceRange.levelCount = mipLevels;
    barrier.subresourceRange.layerCount = 1U;

    VkPipelineStageFlags sourceStage, destinationStage;

    if (VK_IMAGE_LAYOUT_UNDEFINED == oldLayout && VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL == newLayout)
    {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    
    else if (VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL == oldLayout && VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == newLayout)
    {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        // TODO: where do we want unstaged writes? (frequent texture updates)
    }
    
    else if (VK_IMAGE_LAYOUT_UNDEFINED == oldLayout && VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL == newLayout)
    {
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }

    else if (VK_IMAGE_LAYOUT_UNDEFINED == oldLayout  && VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL == newLayout)
    {
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }

    else
    {
        CoronaLog( "Unsupported layout transition! ");

        return false;
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0U, NULL, // memory
        0U, NULL, // memory buffer
        1U, &barrier // image memory
    );

    fState->EndSingleTimeCommands( commandBuffer );

    return true;
}

VkImageView
VulkanTexture::CreateImageView( VulkanState * state, VkImage image, VkFormat format, VkImageAspectFlags flags, uint32_t mipLevels, const VkComponentMapping * componentMapping )
{
	VkImageViewCreateInfo createImageViewInfo = {};

	createImageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;

    if (componentMapping)
    {
        createImageViewInfo.components = *componentMapping;
    }

    else
    {
	    createImageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
	    createImageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
	    createImageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
	    createImageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    }

	createImageViewInfo.format = format;
	createImageViewInfo.image = image;
	createImageViewInfo.subresourceRange.aspectMask = flags;
	createImageViewInfo.subresourceRange.baseArrayLayer = 0;
	createImageViewInfo.subresourceRange.baseMipLevel = 0;
	createImageViewInfo.subresourceRange.layerCount = 1;
	createImageViewInfo.subresourceRange.levelCount = mipLevels;
	createImageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;

	VkImageView view;

	if (VK_SUCCESS == vkCreateImageView( state->GetDevice(), &createImageViewInfo, state->GetAllocationCallbacks(), &view ))
	{
        return view;
    }

    else
    {
        CoronaLog( "Failed to create image view!" );

        return VK_NULL_HANDLE;
    }
}

/*
void generateMipmaps(VkImage image, VkFormat imageFormat, int32_t texWidth, int32_t texHeight, uint32_t mipLevels) {
    // Check if image format supports linear blitting
    VkFormatProperties formatProperties;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, imageFormat, &formatProperties);

    if (!(formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) {
        throw std::runtime_error("texture image format does not support linear blitting!");
    }

    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier = {};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.levelCount = 1;

    int32_t mipWidth = texWidth;
    int32_t mipHeight = texHeight;

    for (uint32_t i = 1; i < mipLevels; i++) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        VkImageBlit blit = {};
        blit.srcOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = i - 1;
        blit.srcSubresource.baseArrayLayer = 0;
        blit.srcSubresource.layerCount = 1;
        blit.dstOffsets[0] = {0, 0, 0};
        blit.dstOffsets[1] = { mipWidth > 1 ? mipWidth / 2 : 1, mipHeight > 1 ? mipHeight / 2 : 1, 1 };
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.mipLevel = i;
        blit.dstSubresource.baseArrayLayer = 0;
        blit.dstSubresource.layerCount = 1;

        vkCmdBlitImage(commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        if (mipWidth > 1) mipWidth /= 2;
        if (mipHeight > 1) mipHeight /= 2;
    }

    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    endSingleTimeCommands(commandBuffer);
}
*/

/*
    VkFormat findSupportedFormat(const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features) {
        for (VkFormat format : candidates) {
            VkFormatProperties props;
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

            if (tiling == VK_IMAGE_TILING_LINEAR && (props.linearTilingFeatures & features) == features) {
                return format;
            } else if (tiling == VK_IMAGE_TILING_OPTIMAL && (props.optimalTilingFeatures & features) == features) {
                return format;
            }
        }

        throw std::runtime_error("failed to find supported format!");
    }
*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------