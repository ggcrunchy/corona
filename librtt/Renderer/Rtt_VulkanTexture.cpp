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
/*
	void getFormatTokens( Texture::Format format, GLint& internalFormat, GLenum& sourceFormat, GLenum& sourceType )
	{
		switch( format )
		{
			case Texture::kAlpha:		internalFormat = GL_ALPHA;		sourceFormat = GL_ALPHA;		sourceType = GL_UNSIGNED_BYTE; break;
			case Texture::kLuminance:	internalFormat = GL_LUMINANCE;	sourceFormat = GL_LUMINANCE;	sourceType = GL_UNSIGNED_BYTE; break;
			case Texture::kRGB:			internalFormat = GL_RGB;		sourceFormat = GL_RGB;			sourceType = GL_UNSIGNED_BYTE; break;
			case Texture::kRGBA:		internalFormat = GL_RGBA;		sourceFormat = GL_RGBA;			sourceType = GL_UNSIGNED_BYTE; break;
#if defined( Rtt_WIN_PHONE_ENV )
			case Texture::kBGRA:		internalFormat = GL_BGRA_EXT;	sourceFormat = GL_BGRA_EXT;		sourceType = GL_UNSIGNED_BYTE; break;
#elif !defined( Rtt_OPENGLES )
			case Texture::kARGB:		internalFormat = GL_RGBA8;		sourceFormat = GL_BGRA;			sourceType = GL_UNSIGNED_INT_8_8_8_8_REV; break;
			case Texture::kBGRA:		internalFormat = GL_RGBA8;		sourceFormat = GL_BGRA;			sourceType = GL_UNSIGNED_INT_8_8_8_8; break;
			case Texture::kABGR:		internalFormat = GL_ABGR_EXT;	sourceFormat = GL_ABGR_EXT;		sourceType = GL_UNSIGNED_BYTE; break;
#else
			// NOTE: These are not available on OpenGL-ES
			// case Texture::kARGB:		internalFormat = GL_RGBA;		sourceFormat = GL_RGBA;			sourceType = GL_UNSIGNED_BYTE; break;
			// case Texture::kBGRA:		internalFormat = GL_RGBA;		sourceFormat = GL_RGBA;			sourceType = GL_UNSIGNED_BYTE; break;
			// case Texture::kABGR:		internalFormat = GL_RGBA;		sourceFormat = GL_RGBA;			sourceType = GL_UNSIGNED_BYTE; break;
#endif

			default: Rtt_ASSERT_NOT_REACHED();
		}
	}

	void getFilterTokens( Texture::Filter filter, GLenum& minFilter, GLenum& magFilter )
	{
		switch( filter )
		{
			case Texture::kNearest:	minFilter = GL_NEAREST;	magFilter = GL_NEAREST;	break;
			case Texture::kLinear:	minFilter = GL_LINEAR;	magFilter = GL_LINEAR;	break;
			default: Rtt_ASSERT_NOT_REACHED();
		}
	}

	GLenum convertWrapToken( Texture::Wrap wrap )
	{
		GLenum result = GL_CLAMP_TO_EDGE;

		switch( wrap )
		{
			case Texture::kClampToEdge:		result = GL_CLAMP_TO_EDGE; break;
			case Texture::kRepeat:			result = GL_REPEAT; break;
			case Texture::kMirroredRepeat:	result = GL_MIRRORED_REPEAT; break;
			default: Rtt_ASSERT_NOT_REACHED();
		}

		return result;
	}*/
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


	
	const U32 w = texture->GetWidth();
	const U32 h = texture->GetHeight();
	const U8* pixels = texture->GetData();

	VkImageCreateInfo imageInfo = {};

	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.extent.width = w;
	imageInfo.extent.height = h;
	imageInfo.extent.depth = 1;
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
		
	VkBuffer stagingBuffer;
	VkDeviceMemory stagingBufferMemory;

// The buffer should be in host visible memory so that we can map it and it should be usable as a transfer source so that we can copy it to an image later on:

// createBuffer(imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, stagingBuffer, stagingBufferMemory);

// We can then directly copy the pixel values that we got from the image loading library to the buffer:
	
	VkDevice device = fState->GetDevice();
	VkDeviceSize imageSize = w * h * 4U;
	void * data;

	vkMapMemory( device, stagingBufferMemory, 0, imageSize, 0, &data );
    memcpy( data, pixels, static_cast< size_t >( imageSize ) );
	vkUnmapMemory( device, stagingBufferMemory );

	/*
	GLuint name = 0;
	glGenTextures( 1, &name );
	fHandle = NameToHandle( name );
	GL_CHECK_ERROR();

	GLenum minFilter;
	GLenum magFilter;
	getFilterTokens( texture->GetFilter(), minFilter, magFilter );

	glBindTexture( GL_TEXTURE_2D, name );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter );
	GL_CHECK_ERROR();

	GLenum wrapS = convertWrapToken( texture->GetWrapX() );
	GLenum wrapT = convertWrapToken( texture->GetWrapY() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT );
	GL_CHECK_ERROR();

	GLint internalFormat;
	GLenum format;
	GLenum type;
	Texture::Format textureFormat = texture->GetFormat();
	getFormatTokens( textureFormat, internalFormat, format, type );

	const U32 w = texture->GetWidth();
	const U32 h = texture->GetHeight();
	const U8* data = texture->GetData();
	{
#if defined( Rtt_EMSCRIPTEN_ENV )
		glPixelStorei( GL_UNPACK_ALIGNMENT, texture->GetByteAlignment() );
		GL_CHECK_ERROR();
#endif

		// It is valid to pass a NULL pointer, so allocation is done either way
		glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data );
		GL_CHECK_ERROR();
		
		fCachedFormat = internalFormat;
		fCachedWidth = w;
		fCachedHeight = h;
	}
	texture->ReleaseData();

	DEBUG_PRINT( "%s : OpenGL name: %d\n",
					Rtt_FUNCTION,
					name );*/
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
/*
GLuint
GLTexture::GetName()
{
	return HandleToName( fHandle );
}*/

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------