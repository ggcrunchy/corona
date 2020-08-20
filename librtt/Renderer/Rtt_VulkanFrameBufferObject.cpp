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
/*
#define ENABLE_DEBUG_PRINT	0

#if ENABLE_DEBUG_PRINT
	#define DEBUG_PRINT( ... ) Rtt_LogException( __VA_ARGS__ );
#else
	#define DEBUG_PRINT( ... )
#endif
*/

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
PrepareAttachmentDescription( VkFormat format, bool noClear, VkAttachmentStoreOp storeOp )
{
	VkAttachmentDescription attachment = {};
	
	attachment.format = format;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment.loadOp = noClear ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment.storeOp = storeOp;

	return attachment;
}

void
RenderPassBuilder::AddColorAttachment( VkFormat format, const AttachmentOptions & options )
{
	VkAttachmentDescription colorAttachment = PrepareAttachmentDescription( format, options.noClear, VK_ATTACHMENT_STORE_OP_STORE );

	if (options.isResolve)
	{
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.samples = options.samples;

		AddAttachment( colorAttachment, fResolveReferences, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
	}

	else
	{
		AddAttachment( colorAttachment, fColorReferences, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	}
}


void
RenderPassBuilder::AddDepthStencilAttachment( VkFormat format, const AttachmentOptions & options )
{
	VkAttachmentDescription depthAttachment = PrepareAttachmentDescription( format, options.noClear, VK_ATTACHMENT_STORE_OP_DONT_CARE );

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
	fImage( VK_NULL_HANDLE ),
	fRenderPassData( NULL )
{
}

void 
VulkanFrameBufferObject::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kFrameBufferObject == resource->GetType() );

	Update( resource );

	/*
	GLuint name;
	glGenFramebuffers( 1, &name );
	fHandle = NameToHandle( name );
	GL_CHECK_ERROR();

	Update( resource );

	DEBUG_PRINT( "%s : OpenGL name: %d\n",
					__FUNCTION__,
					name );*/
}

void 
VulkanFrameBufferObject::Update( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kFrameBufferObject == resource->GetType() );
	Texture * texture = static_cast< FrameBufferObject * >( resource )->GetTexture();

	fExtent.width = texture->GetWidth();
	fExtent.height = texture->GetHeight();

	CleanUpImageData();

	VulkanState * state = fRenderer.GetState();
	const std::vector< VkImage > & swapchainImages = fRenderer.GetSwapchainImages();
	
	RenderPassBuilder builder;

	if (Texture::kNumFilters == texture->GetFilter()) // swapchain
	{
		const VulkanState::SwapchainDetails & details = state->GetSwapchainDetails();
		VkFormat format = details.fFormat.format;

		for (size_t i = 0; i < swapchainImages.size(); ++i)
		{
			fImageData[i].fViews.push_back( VulkanTexture::CreateImageView( state, swapchainImages[i], format, VK_IMAGE_ASPECT_COLOR_BIT, 1U ) );
		}

		builder.AddColorAttachment( format );

		RenderPassBuilder::AttachmentOptions options;

		options.isResolve = true;
		options.samples = (VkSampleCountFlagBits)state->GetSampleCountFlags();

		builder.AddColorAttachment( format, options );
	}

	else
	{
		VulkanTexture * vulkanTexture = static_cast< VulkanTexture * >( texture->GetGPUResource() ); // n.b. GLFrameBufferObject says this will already exist
		VkImageView view = vulkanTexture->GetImageView();

		for (size_t i = 0; i < swapchainImages.size(); ++i)
		{
			fImageData[i].fViews.push_back( view );
		}

		// TODO: settings...
	}

	RenderPassKey key;

	builder.GetKey( key );

	fRenderPassData = state->FindRenderPassData( key );

	VkDevice device = state->GetDevice();
	const VkAllocationCallbacks * allocator = state->GetAllocator();

	if (!fRenderPassData)
	{
		VkRenderPass renderPass = builder.Build( device, allocator );

		fRenderPassData = state->AddRenderPass( key, renderPass );
	}

	for (auto & imageData : fImageData)
	{
        VkFramebufferCreateInfo createFramebufferInfo = {};

        createFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createFramebufferInfo.renderPass = fRenderPassData->fPass;
        createFramebufferInfo.attachmentCount = imageData.fViews.size();
        createFramebufferInfo.height = fExtent.width;
        createFramebufferInfo.layers = 1U;
        createFramebufferInfo.pAttachments = imageData.fViews.data();
        createFramebufferInfo.width = fExtent.height;

        if (vkCreateFramebuffer( device, &createFramebufferInfo, allocator, &imageData.fFramebuffer ) != VK_SUCCESS)
		{
            CoronaLog( "Failed to create framebuffer!" );

			// TODO?
        }
	}
/*
	// Query the bound FBO so that it can be restored. It may (or
	// may not) be worth passing this value in to avoid the query.
	GLint currentFBO;
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &currentFBO );
	{
		Rtt_ASSERT( CPUResource::kFrameBufferObject == resource->GetType() );
		FrameBufferObject* fbo = static_cast<FrameBufferObject*>( resource );

		// The Texture into which data will be rendered must be fully 
		// created prior to being attached to this FrameBufferObject.
		Texture* texture = fbo->GetTexture();
		Rtt_ASSERT( texture );

		GLTexture* gl_texture = static_cast< GLTexture * >( texture->GetGPUResource() );
		Rtt_ASSERT( gl_texture );

		GLuint texture_name = gl_texture->GetName();
		Rtt_ASSERT( texture_name );

		// Attach the destination Texture to this FrameBufferObject.
		glBindFramebuffer( GL_FRAMEBUFFER, GetName() );

		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture_name, 0 );

		GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
		if( status != GL_FRAMEBUFFER_COMPLETE )
		{
			GL_LOG_ERROR( "Incomplete FrameBufferObject" );
		}
	}
	glBindFramebuffer( GL_FRAMEBUFFER, currentFBO );
	GL_CHECK_ERROR();*/
}

void 
VulkanFrameBufferObject::Destroy()
{/*
	GLuint name = GetName();
	if ( 0 != name )
	{
		glDeleteFramebuffers( 1, &name );
		GL_CHECK_ERROR();
		fHandle = 0;
	}

	DEBUG_PRINT( "%s : OpenGL name: %d\n",
					__FUNCTION__,
					name );*/

	CleanUpImageData();

	VulkanState * state = fRenderer.GetState();

	vkDestroyImage( state->GetDevice(), fImage, state->GetAllocator() );

	fImage = VK_NULL_HANDLE;
}

void
VulkanFrameBufferObject::Bind( uint32_t index, VkRenderPassBeginInfo & passBeginInfo, U32 & id )
{
	passBeginInfo.framebuffer = fImageData[index].fFramebuffer;
	passBeginInfo.renderArea.extent = fExtent;
	passBeginInfo.renderPass = fRenderPassData->fPass;
	id = fRenderPassData->fID;
}

void
VulkanFrameBufferObject::CleanUpImageData()
{
	VulkanState * state = fRenderer.GetState();
	VkDevice device = state->GetDevice();
	const VkAllocationCallbacks * allocator = state->GetAllocator();
	const std::vector< VkImage > & swapchainImages = fRenderer.GetSwapchainImages();

	if (VK_NULL_HANDLE == fImage) // swapchain
	{
		for (auto & imageData : fImageData)
		{
			for (VkImageView view : imageData.fViews)
			{
				vkDestroyImageView( device, view, allocator );
			}
		}
	}

	else
	{
		// TODO (might be the same)
	}

	for (auto & imageData : fImageData)
	{
		vkDestroyFramebuffer( device, imageData.fFramebuffer, allocator );
	}

	fImageData.clear();
}
/*
GLuint
GLFrameBufferObject::GetName()
{
	return HandleToName( fHandle );
}

GLuint
GLFrameBufferObject::GetTextureName()
{
	// IMPORTANT: This returns the GL_COLOR_ATTACHMENT0 for the currently bound
	// fbo. The current fbo isn't necessarily the same as the fbo associated
	// with this class instance. This isn't a problem for now because we're
	// currently ONLY using this function after we bind the correct fbo for
	// this class instance.
	GLint param;
	glGetFramebufferAttachmentParameteriv( GL_FRAMEBUFFER,
											GL_COLOR_ATTACHMENT0,
											GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME,
											&param );
	GL_CHECK_ERROR();

	return param;
}*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
