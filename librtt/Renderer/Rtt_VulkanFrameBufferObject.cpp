//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_VulkanRenderer.h"
#include "Renderer/Rtt_VulkanState.h"
#include "Renderer/Rtt_VulkanFrameBufferObject.h"
#include "Renderer/Rtt_VulkanTexture.h"

#include "Renderer/Rtt_FrameBufferObject.h"
#include "Renderer/Rtt_Texture.h"
#include "Core/Rtt_Assert.h"
#include "CoronaLog.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

static VkAttachmentLoadOp
GetLoadOp( const RenderPassBuilder::AttachmentOptions & options )
{
	if (!options.isResolve)
	{
		return options.noClear ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
	}

	else
	{
		return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}
}

static VkAttachmentDescription
PrepareAttachmentDescription( VkFormat format, bool noClear )
{
	VkAttachmentDescription attachment = {};
	
	attachment.format = format;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.loadOp = noClear ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	return attachment;
}

void
RenderPassBuilder::AddColorAttachment( VkFormat format, const AttachmentOptions & options )
{
	VkAttachmentDescription colorAttachment = PrepareAttachmentDescription( format, options.noClear );

	if (options.isResolve)
	{
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	}

	else
	{
		colorAttachment.samples = options.samples;
	}

	if (options.isPresentable || options.isResolve || options.isResult)
	{
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}

	VkImageLayout finalLayout = options.isPresentable ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_UNDEFINED;

	AddAttachment( colorAttachment, options.isResolve ? fResolveReferences : fColorReferences, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, finalLayout );
}

void
RenderPassBuilder::AddDepthStencilAttachment( VkFormat format, const AttachmentOptions & options )
{
	VkAttachmentDescription depthAttachment = PrepareAttachmentDescription( format, options.noClear );

	AddAttachment( depthAttachment, fDepthStencilReferences, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL );
}

void
RenderPassBuilder::AddSubpassDependency( const VkSubpassDependency & dependency )
{
	fDependencies.push_back( dependency );
}

VkRenderPass
RenderPassBuilder::Build( VkDevice device, const VkAllocationCallbacks * allocator ) const
{
	VkSubpassDescription subpass = {};

	subpass.colorAttachmentCount = fColorReferences.size();
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.pColorAttachments = fColorReferences.data();
	subpass.pDepthStencilAttachment = fDepthStencilReferences.data(); // 0 or 1
	subpass.pResolveAttachments = fResolveReferences.data(); // 0 or same as color references

	VkRenderPassCreateInfo createRenderPassInfo = {};

    createRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createRenderPassInfo.attachmentCount = fDescriptions.size();
    createRenderPassInfo.dependencyCount = fDependencies.size();
    createRenderPassInfo.pAttachments = fDescriptions.data();
    createRenderPassInfo.pDependencies = fDependencies.data();
    createRenderPassInfo.pSubpasses = &subpass;
    createRenderPassInfo.subpassCount = 1U;

	VkRenderPass renderPass = VK_NULL_HANDLE;

    if (VK_SUCCESS == vkCreateRenderPass( device, &createRenderPassInfo, allocator, &renderPass ))
	{
		return renderPass;
	}

	else
	{
        CoronaLog( "Failed to create render pass!" );
		
		return VK_NULL_HANDLE;
    }
}

void
RenderPassBuilder::GetKey( RenderPassKey & key ) const
{
	U8 descriptionCount = U8( fDescriptions.size() ), dependencyCount = U8( fDependencies.size() );

	std::vector< U8 > contents(
		2U + // counts
		descriptionCount * 4U + // descriptions
		dependencyCount * sizeof( VkSubpassDependency ) // dependencies
	);

	contents[0] = descriptionCount;
	contents[1] = dependencyCount;

	int index = 2;

	for ( const VkAttachmentDescription & desc : fDescriptions )
	{
		contents[index++] = U8( desc.format );
		contents[index++] = U8( (desc.loadOp & 0x0F) | ((desc.storeOp << 4U) & 0x0F) );
		contents[index++] = U8( desc.samples );

		if (desc.finalLayout != VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) // non-byte extension enum
		{
			contents[index++] = U8( desc.finalLayout );
		}

		else
		{
			contents[index++] = 0xFF;
		}
	}

	for ( const VkSubpassDependency & dep : fDependencies )
	{
		memcpy( contents.data() + index, &dep, sizeof( VkSubpassDependency ) );

		index += sizeof( VkSubpassDependency );
	}

	key.SetContents( contents );
}

void
RenderPassBuilder::AddAttachment( VkAttachmentDescription & description, std::vector< VkAttachmentReference > & references, VkImageLayout layout, VkImageLayout finalLayout )
{
	VkAttachmentReference attachmentRef = {};

	attachmentRef.attachment = fDescriptions.size();
	attachmentRef.layout = layout;

	references.push_back( attachmentRef );

	description.finalLayout = VK_IMAGE_LAYOUT_UNDEFINED == finalLayout ? layout : finalLayout;
	description.samples = description.samples;

	fDescriptions.push_back( description );
}

TextureSwapchain::TextureSwapchain( Rtt_Allocator * allocator, VulkanState * state )
:	Super( allocator ),
	fState( state )
{
}

TextureSwapchain::~TextureSwapchain()
{
}

U32
TextureSwapchain::GetWidth() const
{
	return fState->GetSwapchainDetails().fExtent.width;
}

U32
TextureSwapchain::GetHeight() const
{
	return fState->GetSwapchainDetails().fExtent.height;
}

VulkanFrameBufferObject::VulkanFrameBufferObject( VulkanRenderer & renderer )
:	fRenderer( renderer ),
	fRenderPassData( NULL )
{
}

void 
VulkanFrameBufferObject::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kFrameBufferObject == resource->GetType() );

	Update( resource );
}

void 
VulkanFrameBufferObject::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kFrameBufferObject == resource->GetType() );
	Texture * texture = static_cast< FrameBufferObject * >( resource )->GetTexture();

	fExtent.width = texture->GetWidth();
	fExtent.height = texture->GetHeight();

	CleanUpImageData();

	bool isSwapchain = Texture::kNumFilters == texture->GetFilter(), wantMultisampleResources = true;
	auto ci = fRenderer.GetState()->GetCommonInfo();
	VkComponentMapping mapping = {};
	VkFormat format = isSwapchain ? ci.state->GetSwapchainDetails().fFormat.format : VulkanTexture::GetVulkanFormat( texture->GetFormat(), mapping );

	RenderPassBuilder builder;

	if (wantMultisampleResources)
	{
		VkSampleCountFlagBits sampleCount = VkSampleCountFlagBits( ci.state->GetSampleCountFlags() );
		VulkanTexture::ImageData color = VulkanTexture::CreateImage(
			ci.state,
			fExtent.width, fExtent.height, 1U,
			sampleCount,
			format,
			VK_IMAGE_TILING_OPTIMAL,
			VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
		);
		VkImageView colorView = VulkanTexture::CreateImageView( ci.state, color.fImage, format, VK_IMAGE_ASPECT_COLOR_BIT, 1U );

		VulkanTexture::TransitionImageLayout( ci.state, color.fImage, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1U );

		fImages.push_back( color.fImage );
		fMemory.push_back( color.fMemory );
		fImageViews.push_back( colorView );

		RenderPassBuilder::AttachmentOptions options;

		options.samples = sampleCount;

		builder.AddColorAttachment( format, options );
	}

	RenderPassBuilder::AttachmentOptions finalResultOptions;

	size_t currentSize = fImageViews.size();

	if (isSwapchain)
	{
		for (VkImage swapchainImage : fRenderer.GetSwapchainImages())
		{
			VkImageView swapchainView = VulkanTexture::CreateImageView( ci.state, swapchainImage, format, VK_IMAGE_ASPECT_COLOR_BIT, 1U );

			fImageViews.push_back( swapchainView );
		}
	}
	
	else
	{
		VulkanTexture * vulkanTexture = static_cast< VulkanTexture * >( texture->GetGPUResource() );

		fImageViews.push_back( vulkanTexture->GetImageView() );
	}

	size_t count = fImageViews.size() - currentSize;

	finalResultOptions.isPresentable = isSwapchain;
	finalResultOptions.isResolve = wantMultisampleResources;

	builder.AddColorAttachment( format, finalResultOptions );

	RenderPassKey key;

	builder.GetKey( key );

	fRenderPassData = ci.state->FindRenderPassData( key );

	if (!fRenderPassData)
	{
		VkRenderPass renderPass = builder.Build( ci.device, ci.allocator );

		fRenderPassData = ci.state->AddRenderPass( key, renderPass );
	}

	for (size_t i = 0; i < count; ++i)
	{
        VkFramebufferCreateInfo createFramebufferInfo = {};

        createFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createFramebufferInfo.renderPass = fRenderPassData->fPass;
        createFramebufferInfo.attachmentCount = currentSize + 1U; // n.b. ignore "extra" image views, cf. note a few lines below
        createFramebufferInfo.height = fExtent.height;
        createFramebufferInfo.layers = 1U;
        createFramebufferInfo.pAttachments = fImageViews.data();
        createFramebufferInfo.width = fExtent.width;
// TODO: look into VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT...
		VkFramebuffer framebuffer = VK_NULL_HANDLE;

        if (VK_SUCCESS == vkCreateFramebuffer( ci.device, &createFramebufferInfo, ci.allocator, &framebuffer ))
		{
			fFramebuffers.push_back( framebuffer );

			if (isSwapchain && i + 1 != count) // move relevant swapchain image view to front; will only be used afterward for cleanup, so order only matters here
			{
				VkImageView temp = fImageViews[currentSize];

				fImageViews[currentSize] = fImageViews[currentSize + i + 1];
				fImageViews[currentSize + i + 1] = temp;
			}
		}

		else
		{
            CoronaLog( "Failed to create framebuffer!" );

			// TODO?
        }
	}

	if (!isSwapchain)
	{
		fImageViews.pop_back(); // owned by texture
	}
}

void 
VulkanFrameBufferObject::Destroy()
{
	CleanUpImageData();
}

void
VulkanFrameBufferObject::Bind( VulkanRenderer & renderer, uint32_t index, VkRenderPassBeginInfo & passBeginInfo )
{
	passBeginInfo.framebuffer = fFramebuffers[index];
	passBeginInfo.renderArea.extent = fExtent;
	passBeginInfo.renderPass = fRenderPassData->fPass;

	fRenderer.SetRenderPass( fRenderPassData->fID, fRenderPassData->fPass );
}

void
VulkanFrameBufferObject::CleanUpImageData()
{
	auto ci = fRenderer.GetState()->GetCommonInfo();

	for (VkFramebuffer framebuffer : fFramebuffers)
	{
		vkDestroyFramebuffer( ci.device, framebuffer, ci.allocator );
	}

	for (VkImageView view : fImageViews)
	{
		vkDestroyImageView( ci.device, view, ci.allocator );
	}

	for (VkImage image : fImages)
	{
		vkDestroyImage( ci.device, image, ci.allocator );
	}

	for (VkDeviceMemory memory : fMemory)
	{
		vkFreeMemory( ci.device, memory, ci.allocator );
	}

	fFramebuffers.clear();
	fImageViews.clear();
	fImages.clear();
	fMemory.clear();
}

/*
    void createColorResources() {
        VkFormat colorFormat = swapChainImageFormat;

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, colorFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, colorImage, colorImageMemory);
        colorImageView = createImageView(colorImage, colorFormat, VK_IMAGE_ASPECT_COLOR_BIT, 1);

        transitionImageLayout(colorImage, colorFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 1);
    }

    void createDepthResources() {
        VkFormat depthFormat = findDepthFormat();

        createImage(swapChainExtent.width, swapChainExtent.height, 1, msaaSamples, depthFormat, VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, depthImage, depthImageMemory);
        depthImageView = createImageView(depthImage, depthFormat, VK_IMAGE_ASPECT_DEPTH_BIT, 1);

        transitionImageLayout(depthImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, 1);
    }

    VkFormat findDepthFormat() {
        return findSupportedFormat(
            {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT},
            VK_IMAGE_TILING_OPTIMAL,
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT
        );
    }
*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
