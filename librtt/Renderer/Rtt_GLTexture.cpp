//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Config.h"

#include "Renderer/Rtt_GLTexture.h"

#include "Renderer/Rtt_GL.h"
#include "Renderer/Rtt_Texture.h"
#include "Core/Rtt_Assert.h"

#include "Rtt_Profiling.h"
#include "Core/Rtt_Array.h"
#include "Core/Rtt_String.h"
#include "Renderer/Rtt_Renderer.h"
#include "Display/Rtt_Display.h"
#if defined( Rtt_EGL )
	#include <EGL/egl.h>
#endif

// ----------------------------------------------------------------------------

#define ENABLE_DEBUG_PRINT    0

#if ENABLE_DEBUG_PRINT
    #define DEBUG_PRINT( ... ) Rtt_LogException( __VA_ARGS__ );
#else
    #define DEBUG_PRINT( ... )
#endif


#if !defined(Rtt_OPENGLES) && !defined(GL_ABGR_EXT)
#define GL_ABGR_EXT 0x8000
#endif
// ----------------------------------------------------------------------------

namespace /*anonymous*/
{
    using namespace Rtt;

    void getFormatTokens( Texture::Format format, GLint& internalFormat, GLenum& sourceFormat, GLenum& sourceType )
    {
		switch( format.GetValue() )
        {
            case Texture::kAlpha:        internalFormat = GL_ALPHA;        sourceFormat = GL_ALPHA;        sourceType = GL_UNSIGNED_BYTE; break;
#if defined( Rtt_MetalANGLE)
            case Texture::kLuminance:   internalFormat = GL_RED_EXT;    sourceFormat = GL_RED_EXT;        sourceType = GL_UNSIGNED_BYTE; break;
#else
            case Texture::kLuminance:    internalFormat = GL_LUMINANCE;    sourceFormat = GL_LUMINANCE;    sourceType = GL_UNSIGNED_BYTE; break;
#endif
            case Texture::kRGB:            internalFormat = GL_RGB;        sourceFormat = GL_RGB;            sourceType = GL_UNSIGNED_BYTE; break;
            case Texture::kRGBA:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
#if defined( Rtt_WIN_PHONE_ENV )
            case Texture::kBGRA:        internalFormat = GL_BGRA_EXT;    sourceFormat = GL_BGRA_EXT;        sourceType = GL_UNSIGNED_BYTE; break;
#elif !defined( Rtt_OPENGLES )
            case Texture::kARGB:        internalFormat = GL_RGBA8;        sourceFormat = GL_BGRA;            sourceType = GL_UNSIGNED_INT_8_8_8_8_REV; break;
            case Texture::kBGRA:        internalFormat = GL_RGBA8;        sourceFormat = GL_BGRA;            sourceType = GL_UNSIGNED_INT_8_8_8_8; break;
            #ifdef GL_ABGR_EXT
            case Texture::kABGR:
                internalFormat = GL_ABGR_EXT;
                sourceFormat = GL_ABGR_EXT;
                sourceType = GL_UNSIGNED_BYTE;
                break;
            #endif
#else
            // NOTE: These are not available on OpenGL-ES
            // case Texture::kARGB:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
            // case Texture::kBGRA:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
            // case Texture::kABGR:        internalFormat = GL_RGBA;        sourceFormat = GL_RGBA;            sourceType = GL_UNSIGNED_BYTE; break;
#endif
#ifdef Rtt_NXS_ENV
            case Texture::kLuminanceAlpha:        internalFormat = GL_LUMINANCE_ALPHA;    sourceFormat = GL_LUMINANCE_ALPHA;        sourceType = GL_UNSIGNED_BYTE; break;
#endif
            default: Rtt_ASSERT_NOT_REACHED();
        }
    }

    void getFilterTokens( Texture::Filter filter, GLenum& minFilter, GLenum& magFilter )
    {
        switch( filter )
        {
            case Texture::kNearest:    minFilter = GL_NEAREST;    magFilter = GL_NEAREST;    break;
            case Texture::kLinear:    minFilter = GL_LINEAR;    magFilter = GL_LINEAR;    break;
            default: Rtt_ASSERT_NOT_REACHED();
        }
    }

    GLenum convertWrapToken( Texture::Wrap wrap )
    {
        GLenum result = GL_CLAMP_TO_EDGE;

        switch( wrap )
        {
            case Texture::kClampToEdge:        result = GL_CLAMP_TO_EDGE; break;
            case Texture::kRepeat:            result = GL_REPEAT; break;
            case Texture::kMirroredRepeat:    result = GL_MIRRORED_REPEAT; break;
            default: Rtt_ASSERT_NOT_REACHED();
        }

        return result;
    }
}

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

GLint CalculateOptimalAlignment(U32 width, GLenum format)
{
    int bytesPerPixel = 4;
    
    switch(format) {
        case GL_ALPHA:
#if defined( Rtt_MetalANGLE)
        case GL_RED_EXT:
#else
        case GL_LUMINANCE:
#endif
            bytesPerPixel = 1;
            break;
            
        case GL_RGB:
            bytesPerPixel = 3;
            break;
            
        
        #if defined(GL_ABGR_EXT)
        case GL_ABGR_EXT:
        #endif
        case GL_RGBA:
            bytesPerPixel = 4;
            break;
            
#ifdef Rtt_NXS_ENV
        case GL_LUMINANCE_ALPHA:
            bytesPerPixel = 2;
            break;
#endif
        default: // Default
            bytesPerPixel = 4;
            break;
    }

    const U32 rowBytes = width * bytesPerPixel;
   
    if(rowBytes % 8 == 0) return 8;
    if(rowBytes % 4 == 0) return 4;
    if(rowBytes % 2 == 0) return 2;
    
    return 1; // No alignment
}

static bool HasLinearFiltering( int index );
static bool IsCompressed( int index );

struct TextureFormatInfo {
	enum {
		kIsRenderable = 0x01,
		kIsDepthRelated = 0x02,
		kIsStencilRelated = 0x04,
		kIsCompressed = 0x08,
		kHasLinearFiltering = 0x10,
		
		kGreenBit = 0x20,
		kBlueBit = 0x40,
		kAlphaBit = 0x80

		// TODO: some of this should be known, either due to the
		// particular backend or because some extension is present,
		// so we could avoid doing a redundant FBO attach...
	};
	
	void Initialize( const char* name, GLenum source, GLenum internal = 0 )
	{
		Rtt_ASSERT( (GLenum)(GLushort)source == source );
		Rtt_ASSERT( (GLenum)(GLushort)internal == internal );
	
		fName = name; // assumed to be static
		fSource = (GLushort)source;
		fInternal = (GLushort)( internal ? internal : source );
		
		switch ( source )
		{
		case GL_RGBA:
			fFlags |= kAlphaBit; // ...and fallthrough
		case GL_RGB:
			fFlags |= kBlueBit; // ditto
	#if GL_ARB_texture_rg
		case GL_RG:
    #elif defined(GL_EXT_texture_rg)
        case GL_RG_EXT:
    #endif
			fFlags |= kGreenBit;
		}
	}
	
	void InitializeBlocked( const char* name, GLenum source, U8 width, U8 height, U8 blockSize )
	{
		Initialize( name, source );
		
		fBlockWidth = width;
		fBlockHeight = height;
		fBlockSizeInBytes = blockSize;
	}

	// cf. getFormatTokens()
	const char* fName;
	GLushort fType; // ignored if compressed
	GLushort fSource; // ditto
	GLushort fInternal; // matches source on ES 2.0
	GLushort fFlags;
	U8 fBlockWidth; // for various compressed formats (we COULD repurpose fType and fSource...)
	U8 fBlockHeight;
	U8 fBlockSizeInBytes;
		// ASTC: 128 bits in any
			// various
		// DXT1: 8 bytes
		// DXT3, 5: 16
		// ^^ 4x4
		// BC*
		// ETC1:
			// The number of bits that represent a 4x4 texe	l block is 64 bits if
			// <internalformat> is given by ETC1_RGB8_OES.
		// ETC2: 4x4, 8 bytes
		// EAC: r, rg...
};

static TextureFormatInfo sInfo[32]; // arbitrary size; expand as necessary

static void
RoundDimension( int& value, int dim )
{
	value--;
	
	value += dim - value % dim;
}

static void
GetCompressedInputs( const TextureFormatInfo& info, int& width, int& height, GLsizei& imageSize )
{
	if ( 0 != info.fBlockWidth )
	{
		Rtt_ASSERT( 0 != info.fBlockHeight );
		
		RoundDimension( width, (int)( info.fBlockWidth ) );
		RoundDimension( height, (int)( info.fBlockHeight ) );
		
		imageSize = (GLsizei)( width * height );
		
		imageSize *= info.fBlockSizeInBytes;
		imageSize /= info.fBlockWidth * info.fBlockHeight;
	}
}

void
GLTexture::Create( CPUResource* resource )
{
    Rtt_ASSERT( CPUResource::kTexture == resource->GetType() || CPUResource::kVideoTexture == resource->GetType() );
    Texture* texture = static_cast<Texture*>( resource );

    SUMMED_TIMING( gltc, "Texture GPU Resource: Create" );

    GLuint name = 0;
    glGenTextures( 1, &name );
    fHandle = NameToHandle( name );
    GL_CHECK_ERROR();

    GLenum minFilter;
    GLenum magFilter;
    getFilterTokens( texture->GetFilter(), minFilter, magFilter );

	fUsingLinearFiltering = Texture::kLinear == texture->GetFilter();

	U16 formatIndex = 0;
	texture->GetFormat().GetValue( &formatIndex );
	if ( 0 != formatIndex && fUsingLinearFiltering && !HasLinearFiltering( formatIndex ) )
	{
		getFilterTokens( Texture::kNearest, minFilter, magFilter );
		
		fUsingLinearFiltering = false;
	}

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
    
	if ( 0 != formatIndex )
	{
		internalFormat = sInfo[formatIndex].fInternal;
		format = sInfo[formatIndex].fSource;
		type = sInfo[formatIndex].fType;
	}
	else
	{
		Texture::Format textureFormat = texture->GetFormat();
		getFormatTokens( textureFormat, internalFormat, format, type );
	}
    const U32 w = texture->GetWidth();
    const U32 h = texture->GetHeight();
    const U8* data = texture->GetData();
    {
//#if defined( Rtt_EMSCRIPTEN_ENV )
//        glPixelStorei( GL_UNPACK_ALIGNMENT, texture->GetByteAlignment() );
//        GL_CHECK_ERROR();
//#endif
        glPixelStorei(GL_UNPACK_ALIGNMENT, CalculateOptimalAlignment(w, internalFormat));
        GL_CHECK_ERROR();

        // It is valid to pass a NULL pointer, so allocation is done either way
		if ( 0 != formatIndex && IsCompressed( formatIndex ) )
		{
			// TODO: type / size?
			// imageSize = w * h * bpp?
			glCompressedTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, type, data );
		}
		else
		{
			glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data );
		}
        GL_CHECK_ERROR();
        
        fCachedFormat = internalFormat;
        fCachedWidth = w;
        fCachedHeight = h;
    }
    texture->ReleaseData();

    DEBUG_PRINT( "%s : OpenGL name: %d\n",
                    Rtt_FUNCTION,
                    name );
}

void
GLTexture::Update( CPUResource* resource )
{
    Rtt_ASSERT( CPUResource::kTexture == resource->GetType() );
    Texture* texture = static_cast<Texture*>( resource );

    SUMMED_TIMING( gltu, "Texture GPU Resource: Update" );

    const U8* data = texture->GetData();
    if( data )
    {
        const U32 w = texture->GetWidth();
        const U32 h = texture->GetHeight();

		U16 formatIndex = 0;
		
		texture->GetFormat().GetValue( &formatIndex );

        GLint internalFormat;
        GLenum format;
        GLenum type;
		if ( 0 != formatIndex )
		{
			internalFormat = sInfo[formatIndex].fInternal;
			format = sInfo[formatIndex].fSource;
			type = sInfo[formatIndex].fType;
		}
		else
		{
			getFormatTokens( texture->GetFormat(), internalFormat, format, type );
		}
        glBindTexture( GL_TEXTURE_2D, GetName() );

        glPixelStorei(GL_UNPACK_ALIGNMENT, CalculateOptimalAlignment(w, internalFormat));
        GL_CHECK_ERROR();

        if (internalFormat == fCachedFormat && w == fCachedWidth && h == fCachedHeight )
        {
			if ( 0 != formatIndex && IsCompressed( formatIndex ) )
			{
				// TODO: type = imageSize?
				glCompressedTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data );
			}
			else
			{
				glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, w, h, format, type, data );
			}
        }
        else
        {
			if ( 0 != formatIndex && IsCompressed( formatIndex ) )
			{
				// TODO: size?
				glCompressedTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, type, data );
			}
			else
			{
				glTexImage2D( GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, data );
			}
            fCachedFormat = internalFormat;
            fCachedWidth = w;
            fCachedHeight = h;
        }

		// Apply any filter mode changes, taking into account that many
		// texture formats cannot handle linear filtering.
		bool wantsLinearFiltering = Texture::kLinear == texture->GetFilter();
		if (wantsLinearFiltering) // is it also allowed?
		{
			wantsLinearFiltering = ( 0 == formatIndex ) || HasLinearFiltering( formatIndex );
		}
									
		if ( wantsLinearFiltering != fUsingLinearFiltering )
		{
			Texture::Filter newFilter = wantsLinearFiltering ? Texture::kLinear : Texture::kNearest;
			GLenum minFilter, magFilter;
			getFilterTokens( newFilter, minFilter, magFilter );
			
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter );
			GL_CHECK_ERROR();			

			fUsingLinearFiltering = wantsLinearFiltering;
		}
    }
    texture->ReleaseData();
}

void
GLTexture::Destroy()
{
    GLuint name = GetName();
    if ( 0 != name )
    {
        glDeleteTextures( 1, &name );
        GL_CHECK_ERROR();
        fHandle = 0;
    }

    DEBUG_PRINT( "%s : OpenGL name: %d\n",
                    Rtt_FUNCTION,
                    name );
}

void
GLTexture::Bind( U32 unit )
{
    glActiveTexture( GL_TEXTURE0 + unit );
    glBindTexture( GL_TEXTURE_2D, GetName() );
    GL_CHECK_ERROR();
}

GLuint
GLTexture::GetName()
{
    return HandleToName( fHandle );
}

#if defined( Rtt_OPENGLES )
	#define GL_GET_PROC(name, cap, suffix) (PFNGL ## cap ## suffix ## PROC) eglGetProcAddress( "gl" #name #suffix )
#else
	#define GL_GET_PROC(name, suffix) gl ## name ## suffix
#endif

static int sInfoCount;

static TextureFormatInfo*
AllocInfo( const TextureFormatInfo& form )
{
	if ( sInfoCount < sizeof( sInfo ) / sizeof( *sInfo ) )
	{
		TextureFormatInfo* info = & sInfo[ sInfoCount++ ];
		*info = form;
		
		return info;
	}
	
	else
	{
		Rtt_LogException( "Too many texture formats" );
		
		Rtt_ASSERT_NOT_REACHED();
		
		return NULL;
	}
}

static void
AliasInfo( const TextureFormatInfo& copy, const char* name )
{
	TextureFormatInfo* info = AllocInfo( copy );
	
	info->fName = name;
}

static int
FindInfo( const char* name )
{
	Rtt_ASSERT( NULL != name );
	
	for ( int i = 0; i < sInfoCount; i++ )
	{
		if ( Rtt_StringCompareNoCase( sInfo[i].fName, name ) == 0 )
		{
			return i;
		}
	}
	
	return -1;
}

#if !defined( Rtt_OPENGLES )
	#define GL_CONST( name ) name
#else
	#define GL_CONST( name ) name ## _EXT
#endif

static void
EnumerateSupportedFormats()
{
	TextureFormatInfo form = {};

	form.fType = GL_UNSIGNED_BYTE;
	form.fFlags = TextureFormatInfo::kHasLinearFiltering;

	TextureFormatInfo compressedForm = form;

	compressedForm.fFlags = TextureFormatInfo::kIsCompressed;

	// this is a fallback for index 0; in practice it shouldn't be reached
	// and probably indicates a bug in the calling code
	AllocInfo( form )->Initialize( "", GL_RGBA );

	const char * extensions = (const char *)glGetString( GL_EXTENSIONS );

#if !defined( Rtt_OPENGLES )
	bool hasRG = false;
	#if GL_ARB_texture_rg
		hasRG = NULL != strstr( extensions, "GL_ARB_texture_rg" );
		
		if ( hasRG )
		{
			AllocInfo( form )->Initialize( "red", GL_RED, GL_R8 );
			AllocInfo( form )->Initialize( "rg", GL_RG, GL_RG8 );
		}
	#endif

	#if GL_ARB_texture_float
		bool hasFloats = NULL != strstr( extensions, "GL_ARB_texture_float" );
		if ( hasFloats )
		{
			form.fType = GL_FLOAT;
			
			AllocInfo( form )->Initialize( "rgb16f", GL_RGB, GL_RGB16F_ARB );
			AllocInfo( form )->Initialize( "rgba16f", GL_RGBA, GL_RGBA16F_ARB );
			AllocInfo( form )->Initialize( "rgb16f", GL_RGB, GL_RGB32F_ARB );
			AllocInfo( form )->Initialize( "rgba16f", GL_RGBA, GL_RGBA32F_ARB );
			
			if ( hasRG )
			{
				AllocInfo( form )->Initialize( "r16f", GL_RED, GL_R16F );
				AllocInfo( form )->Initialize( "rg16f", GL_RG, GL_RG16F );
				AllocInfo( form )->Initialize( "r32f", GL_RED, GL_R32F );
				AllocInfo( form )->Initialize( "rg32f", GL_RG, GL_RG32F );
			}
		}
	#endif

	// TODO: do some review of some other APIs and see if they
	// bother with all of these :D
	TextureFormatInfo depthForm = form;
	
	depthForm.fFlags = TextureFormatInfo::kIsDepthRelated;
	depthForm.fType = GL_UNSIGNED_SHORT;
	
	AllocInfo( depthForm )->Initialize( "depth16", GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT16 );
	
	depthForm.fType = GL_UNSIGNED_INT;
	
	AllocInfo( depthForm )->Initialize( "depth24", GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT24 );
	AllocInfo( depthForm )->Initialize( "depth32", GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT32 );
	
	#if GL_ARB_depth_buffer_float
		bool hasDepthBufferFloat = NULL != strstr( extensions, "GL_ARB_depth_buffer_float" );
		if ( hasDepthBufferFloat )
		{
			depthForm.fType = GL_FLOAT;
		
			AllocInfo( depthForm )->Initialize( "depth32f", GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT32F );
		}

		// GL_DEPTH32F_STENCIL8, GL_FLOAT_32_UNSIGNED_INT_24_8_REV
	#endif

	// useful? was just a test for being not renderable:
	// AllocInfo()->Initialize( "luminance", GL_UNSIGNED_BYTE, GL_LUMINANCE );
	
	// no real limit, just boring to do
		// type probably not all that useful in most cases, if not populating from floats, etc.
		// a few could be marked for where that's explicitly wanted
#else
	int esMajorVersion = 2, esMinorVersion = 0;
	// TODO ^^^^
#if defined(GL_OES_compressed_ETC1_RGB8_texture)
	bool hasETC1 = NULL != strstr( extensions, "GL_OES_compressed_ETC1_RGB8_texture" );
	if ( hasETC1 )
    {
        AllocInfo(compressedForm)->InitializeBlocked( "etc1", GL_ETC1_RGB8_OES, 4, 4, 8 );
    }
#endif

	bool hasRG = false;
#if defined(GL_EXT_texture_rg)
    hasRG = NULL != strstr( extensions, "GL_EXT_texture_rg" );
	if ( hasRG )
	{
		AllocInfo( form )->Initialize( "red", GL_RED_EXT );
		AllocInfo( form )->Initialize( "rg", GL_RG_EXT );
	}
#endif

#if defined(GL_OES_texture_half_float)
	bool has16BitFloats = NULL != strstr( extensions, "GL_OES_texture_half_float" ); // TODO: core in 3?
	if (has16BitFloats)
    {
        bool hasLinearFiltering = NULL != strstr( extensions, "GL_OES_texture_half_float_linear" );
		TextureFormatInfo f16Info = {};

        f16Info.fFlags = hasLinearFiltering ? TextureFormatInfo::kHasLinearFiltering : 0;
        f16Info.fType = GL_HALF_FLOAT_OES;

        AllocInfo( f16Info )->Initialize( "rgb16f", GL_RGB16F_EXT );
        AllocInfo( f16Info )->Initialize( "rgba16f", GL_RGBA16F_EXT );

		// also _linear for each?
		// linear for magnification
		// linear + mipmap distinctions for minification

		if ( hasRG )
		{
            AllocInfo( f16Info )->Initialize( "r16f", GL_R16F_EXT );
            AllocInfo( f16Info )->Initialize( "rg16f", GL_RG16F_EXT );
		}
	}
#endif

#if defined(GL_OES_texture_float)
	bool has32BitFloats = NULL != strstr( extensions, "GL_OES_texture_float" ); // TODO: ditto...
	if (has32BitFloats)
	{
        bool hasLinearFiltering = NULL != strstr( extensions, "GL_OES_texture_float_linear" );
		TextureFormatInfo f32Info = {};

        f32Info.fFlags = hasLinearFiltering ? TextureFormatInfo::kHasLinearFiltering : 0;
        f32Info.fType = GL_FLOAT;

        AllocInfo( f32Info )->Initialize( "rgb32f", GL_RGB32F_EXT );
        AllocInfo( f32Info )->Initialize( "rgba32f", GL_RGBA32F_EXT );

		if ( hasRG )
		{
            AllocInfo( f32Info )->Initialize( "r32f", GL_R32F_EXT );
            AllocInfo( f32Info )->Initialize( "rg32f", GL_RG32F_EXT );
		}
	}
#endif

	bool hasDepthTexture = false;
#if defined(GL_OES_depth_texture)
	hasDepthTexture = NULL != strstr( extensions, "GL_OES_depth_texture" );
	// is depth-related
		// has ushort and uint versions?
#endif

#if defined(GL_OES_packed_depth_stencil)
    bool hasPackedDepthStencil = NULL != strstr( extensions, "GL_OES_packed_depth_stencil" );

	if ( hasDepthTexture )
	{
		// TODO!
			// is depth- and stencil-related
			// inverted? or is there nothing to actually add here?
	}
	else
	{
		// TODO!
	}
#endif

    // TODO: interaction of these with previous?
    // these first two are renderbuffer thing, so not able to read them
    // suggests another resource? or anyhow, not a texture and workflow isn't there yet
    /*
	bool hasDepth24 = NULL != strstr( extensions, "GL_OES_depth24" );
	bool hasDepth32 = NULL != strstr( extensions, "GL_OES_depth32" );
	*/
	
	// TODO: if ES3+, i.e. 3 == esMajorVersion
	// https://stackoverflow.com/a/7313411 / https://stackoverflow.com/a/56310581
	// ^^^ some searching suggested ES3 relaxes this, but not confirmed
	// does have implications for external textures, though
	// web? (ANGLE plugins, etc.)
		// TODO: isES3OrBetter()? 3.0, 3.1, 3.2
	
	// GL_EXT_texture_compression_s3tc
	// OES_depth_texture (GL_DEPTH_COMPONENT)
	// OES_packed_depth_stencil (GL_DEPTH24_STENCIL8)

	// astc, etc2 waiting for ES 3
#endif

bool hasS3TC = false, hasDXT1 = false;

#if defined(GL_EXT_texture_compression_s3tc)
	hasS3TC = NULL != strstr( extensions, "GL_EXT_texture_compression_s3tc" );
#endif

#if defined(GL_EXT_texture_compression_dxt1)
	hasDXT1 = NULL != strstr( extensions, "GL_EXT_texture_compression_dxt1" );
#endif

	if ( hasS3TC )
	{
		int currentIndex = sInfoCount;
		
		AllocInfo( compressedForm )->InitializeBlocked( "dxt3", GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, 4, 4, 16 );
		AllocInfo( compressedForm )->InitializeBlocked( "dxt5", GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 4, 4, 16 );
		
		AliasInfo( sInfo[currentIndex++], "bc2" );
		AliasInfo( sInfo[currentIndex++], "bc3" );
	}
	if ( hasS3TC || hasDXT1 )
	{
		int currentIndex = sInfoCount;
		
		AllocInfo( compressedForm )->InitializeBlocked( "dxt1-rgb", GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 8 );
		AllocInfo( compressedForm )->InitializeBlocked( "dxt1-rgba", GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, 4, 8 );
		
		AliasInfo( sInfo[currentIndex++], "bc1-rgb" );
		AliasInfo( sInfo[currentIndex++], "bc1-rgba" );
	}
	
	// GL_ARB/EXT_texture_compression_rgtc
	// GL_ARB_texture_compression_bptc
	// there are also some ANGLE / WebGL things; sRGB
}

// TODO? can skip some of these with ARB_color_buffer_float or similar, per docs...
// also, some seem to be guaranteed

struct FBOCompleteChecks {
	FBOCompleteChecks()
	{
		glGenTextures( 1, &fTexName );
		GL_CHECK_ERROR();
		glGenFramebuffers( 1, &fFBOName );
		GL_CHECK_ERROR();
	}

	~FBOCompleteChecks()
	{
		glDeleteTextures( 1, &fTexName );
		GL_CHECK_ERROR();
		glDeleteFramebuffers( 1, &fFBOName );
		GL_CHECK_ERROR();
	}
	
	bool
	CanAttach( const TextureFormatInfo& info, GLenum attachment = GL_COLOR_ATTACHMENT0 )
	{
		const GLsizei kDim = 1;

		glBindTexture( GL_TEXTURE_2D, fTexName );
		glTexImage2D( GL_TEXTURE_2D, 0, info.fInternal, kDim, kDim, 0, info.fSource, info.fType, NULL );
		GL_CHECK_ERROR();

		// https://community.khronos.org/t/depth-only-fbo-incomplete-draw-buffer/65283/4
	#if !defined(Rtt_OPENGLES)
		if ( GL_COLOR_ATTACHMENT0 != attachment )
		{
			glDrawBuffer( GL_NONE );
			GL_CHECK_ERROR();
			glReadBuffer( GL_NONE );
			GL_CHECK_ERROR();
		}
	#endif

		glBindFramebuffer( GL_FRAMEBUFFER, fFBOName );
		GL_CHECK_ERROR();
		glFramebufferTexture2D( GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, fTexName, 0 );
		GL_CHECK_ERROR();
		GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
		GL_CHECK_ERROR();

		return GL_FRAMEBUFFER_COMPLETE == status;
	}
	
	GLuint fTexName;
	GLuint fFBOName;
};

static bool
IsFormatColorRenderable( int index )
{
	static bool sIsInitialized;

	const int kDepthOrStencilMask = TextureFormatInfo::kIsDepthRelated | TextureFormatInfo::kIsStencilRelated;
	if ( ! sIsInitialized )
	{
		FBOCompleteChecks fboCheck;
		
		for ( int i = 0; i < sInfoCount; i++ )
		{
			if ( sInfo[i].fFlags & kDepthOrStencilMask )
			{
				continue;
			}
			
			if ( sInfo[i].fFlags & TextureFormatInfo::kIsCompressed )
			{
				continue;
			}
			
			if ( fboCheck.CanAttach( sInfo[i] ) )
			{
				sInfo[i].fFlags |= TextureFormatInfo::kIsRenderable;
			}
		}
		
		sIsInitialized = true;
	}
	
	const int kMask = TextureFormatInfo::kIsRenderable | kDepthOrStencilMask;
	return TextureFormatInfo::kIsRenderable == ( sInfo[index].fFlags & kMask );
}

static bool
IsFormatDepthRenderable( int index )
{
	static bool sIsInitialized;
	
	if ( ! sIsInitialized )
	{
		FBOCompleteChecks fboCheck;
	
		for ( int i = 0; i < sInfoCount; i++ )
		{
			if ( !( sInfo[i].fFlags & TextureFormatInfo::kIsDepthRelated ) )
			{
				continue;
			}
			
			if ( fboCheck.CanAttach( sInfo[i], GL_DEPTH_ATTACHMENT ) )
			{
				sInfo[i].fFlags |= TextureFormatInfo::kIsRenderable;
			}
		}

		sIsInitialized = true;
	}

	const int kMask = TextureFormatInfo::kIsRenderable | TextureFormatInfo::kIsDepthRelated;
	return kMask == ( sInfo[index].fFlags & kMask );
}

static bool
IsFormatStencilRenderable( int index )
{
	static bool sIsInitialized;
	
	if ( ! sIsInitialized )
	{
		FBOCompleteChecks fboCheck;
	
		for ( int i = 0; i < sInfoCount; i++ )
		{
			if ( !( sInfo[i].fFlags & TextureFormatInfo::kIsStencilRelated ) )
			{
				continue;
			}
			
			if ( fboCheck.CanAttach( sInfo[i], GL_STENCIL_ATTACHMENT ) )
			{
				sInfo[i].fFlags |= TextureFormatInfo::kIsRenderable;
			}
		}
	
		sIsInitialized = true;
	}

	const int kMask = TextureFormatInfo::kIsRenderable | TextureFormatInfo::kIsStencilRelated;
	return kMask == ( sInfo[index].fFlags & kMask );
}

static bool
HasLinearFiltering( int index )
{
	Rtt_ASSERT( index < sInfoCount );

	return sInfo[index].fFlags & TextureFormatInfo::kHasLinearFiltering;
}

static bool
IsCompressed( int index )
{
	Rtt_ASSERT( index < sInfoCount );
	
	return sInfo[index].fFlags & TextureFormatInfo::kIsCompressed;
}

#undef GL_GET_PROC

typedef bool (*QueryChoice)( int index );

bool
Renderer::QueryTextureInfo( const char* what, const char* name, U16* formatID ) const
{
	struct {
		const char* what;
		QueryChoice func;
	} choices[] = {
		{ "Supported", NULL }, // true if format exists
		{ "ColorRenderable", IsFormatColorRenderable },
		{ "DepthRenderable", IsFormatDepthRenderable },
		{ "StencilRenderable", IsFormatStencilRenderable },
		{ "LinearFilterable", HasLinearFiltering },
		{ "Compressed", IsCompressed }
	};

	Rtt_ASSERT( NULL == choices[0].func );

	int queryChoice = -1;
	for ( int i = 0; i < sizeof( choices ) / sizeof( *choices ); i++ )
	{
		if ( Rtt_StringCompareNoCase( what, choices[i].what ) == 0 )
		{
			queryChoice = i;
			
			break;
		}
	}

	if ( queryChoice >= 0 ) // valid choice found?
	{
		if ( NULL == sInfo->fName ) // not yet populated?
		{
			EnumerateSupportedFormats();
		}

		int index = FindInfo( name );
		if ( index > 0 ) // see note in EnumerateSupportedFormats()
		{
			Rtt_ASSERT( 0 == queryChoice || choices[queryChoice].func );
		
			bool isOK = ( 0 == queryChoice ) || choices[queryChoice].func( index );
			if ( isOK )
			{
				if ( formatID )
				{
					Rtt_ASSERT( (U16)( index ) == index );
				
					*formatID = (U16)( index );
				}
				
				return true;
			}
		}
	}
	else
	{
		Rtt_LogException( "Invalid texture query: '%s'", what );
	}

	return false;
}

void
Renderer::GetNonCoreFormatInfo( U16 formatID, NonCoreFormatInfo& info ) const
{
	Rtt_ASSERT( formatID < sInfoCount );

	// TODO:
	// (This is not yet put to use; ES 2.0, at least,
	// won't distinguish the input vs. texture type,
	// so it's just a half-baked idea and not worth
	// figuring out, e.g. various ushort types.)
	switch ( sInfo[formatID].fType )
	{
	case GL_UNSIGNED_BYTE:
		info.fInputType = NonCoreFormatInfo::kByte;
		break;
	// unsigned short types...
		// return kUint16
	// unsigned int...
		// return kUint32
#if !defined(Rtt_OPENGLES)
	case GL_HALF_FLOAT_ARB:
#elif defined(GL_OES_texture_half_float)
	case GL_HALF_FLOAT_OES:
#endif
		info.fInputType = NonCoreFormatInfo::kFloat16;
		break;
	case GL_FLOAT:
		info.fInputType = NonCoreFormatInfo::kFloat32;
		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}

	if ( NonCoreFormatInfo::kUint16 == info.fInputType )
	{
		info.fNumFloatBits = 0;
		info.fBytesPerComponent = 2;
	}
	else if ( Rtt_StringEndsWith( sInfo[formatID].fName, "16f" ) )
	{
		info.fNumFloatBits = 16;
		info.fBytesPerComponent = 2;
	}
	else if ( Rtt_StringEndsWith( sInfo[formatID].fName, "32f" ) )
	{
		info.fNumFloatBits = 32;
		info.fBytesPerComponent = 4;
	}
	else
	{
		info.fNumFloatBits = 0;
		info.fBytesPerComponent = 1;
	}
	
	info.fRedIndex = 0; // TODO? alpha32f, etc.
	info.fGreenIndex = info.fBlueIndex = info.fAlphaIndex = -1;
	info.fNumComponents = 1;
	
	if ( NonCoreFormatInfo::kUint16 != info.fInputType )
	{
		// TODO? formats with other component arrangements...
		
		if ( sInfo[formatID].fFlags & TextureFormatInfo::kGreenBit )
		{
			info.fGreenIndex = info.fNumComponents++;
		}
		if ( sInfo[formatID].fFlags & TextureFormatInfo::kBlueBit )
		{
			info.fBlueIndex = info.fNumComponents++;
		}
		if ( sInfo[formatID].fFlags & TextureFormatInfo::kAlphaBit )
		{
			info.fAlphaIndex = info.fNumComponents++;
		}
	}
	else
	{
		// TODO! indices are into packed form rather than byte offsets
	}
	
	// TODO? compressed, etc.
}


// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
