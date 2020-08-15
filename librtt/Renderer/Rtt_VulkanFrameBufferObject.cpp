//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

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

VulkanFrameBufferObject::VulkanFrameBufferObject( VulkanState * state, uint32_t imageCount, VkImage * swapchainImages )
:	fState( state ),
	fFramebufferData( imageCount ),
	fImage( VK_NULL_HANDLE ),
	fRenderPass( VK_NULL_HANDLE )
{
	if (swapchainImages)
	{
		const VulkanState::SwapchainDetails & details = fState->GetSwapchainDetails();
		VkFormat format = details.fFormat.format;

		for (uint32_t i = 0; i < imageCount; ++i)
		{
			fFramebufferData[i].fViews.push_back( VulkanTexture::CreateImageView( fState, swapchainImages[i], format, VK_IMAGE_ASPECT_COLOR_BIT, 1U ) );
		}

		RenderPassBuilder builder;

		builder.AddColorAttachment( format );

		RenderPassBuilder::AttachmentOptions options;

		options.isResolve = true;
		options.samples = (VkSampleCountFlagBits)fState->GetSampleCountFlags();

		builder.AddColorAttachment( format, options );

		MakeFramebuffers( details.fExtent.width, details.fExtent.height, builder );
	}
}

void 
VulkanFrameBufferObject::Create( CPUResource* resource )
{
	Rtt_ASSERT( CPUResource::kFrameBufferObject == resource->GetType() );

	Texture * texture = static_cast< FrameBufferObject * >( resource )->GetTexture();
	VulkanTexture * vulkanTexture = static_cast< VulkanTexture * >( texture->GetGPUResource() ); // n.b. GLFrameBufferObject says this will already exist
	VkImageView view = vulkanTexture->GetImageView();

	for (auto & fbData : fFramebufferData)
	{
		fbData.fViews.push_back( view );
	}

	RenderPassBuilder builder; // TODO!

	MakeFramebuffers( texture->GetWidth(), texture->GetHeight(), builder );
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
/*
	for (PerImageData & data : fPerImageData)
	{
		vkDestroyImageView( device, data.view, allocator );
        // TODO: frame buffer

        data.image = VK_NULL_HANDLE;
        data.view = VK_NULL_HANDLE;
	}
*/
}

VulkanFrameBufferObject::Binding 
VulkanFrameBufferObject::Bind( uint32_t index )
{
	Binding binding;

	binding.fFramebuffer = fFramebufferData[index].fFramebuffer;
	binding.fRenderPass = fRenderPass;
	// TODO: clear values

	return binding;
}

void
VulkanFrameBufferObject::MakeFramebuffers( uint32_t width, uint32_t height, const RenderPassBuilder & builder )
{
	RenderPassKey key;

	builder.GetKey( key );
	
	const VulkanState::RenderPassData * data = fState->FindRenderPassData( key );
	const VkAllocationCallbacks * allocator = fState->GetAllocator();
	VkDevice device = fState->GetDevice();

	if (data)
	{
		fRenderPass = data->fPass;
	}

	else
	{
		fRenderPass = builder.Build( device, allocator );

		fState->AddRenderPass( key, fRenderPass );
	}

	for (auto & fbData : fFramebufferData)
	{
        VkFramebufferCreateInfo createFramebufferInfo = {};

        createFramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createFramebufferInfo.renderPass = fRenderPass;
        createFramebufferInfo.attachmentCount = fbData.fViews.size();
        createFramebufferInfo.height = height;
        createFramebufferInfo.layers = 1U;
        createFramebufferInfo.pAttachments = fbData.fViews.data();
        createFramebufferInfo.width = width;

        if (vkCreateFramebuffer( device, &createFramebufferInfo, allocator, &fbData.fFramebuffer ) != VK_SUCCESS)
		{
            CoronaLog( "Failed to create framebuffer!" );

			// TODO?
        }
	}
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