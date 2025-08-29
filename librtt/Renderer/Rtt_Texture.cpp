//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Renderer/Rtt_Texture.h"

#include "Core/Rtt_Assert.h"
#include "Display/Rtt_Display.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

Texture::Format::Format( FormatValue value)
:	fValue( value ),
	fIndex( 0 )
{
}

bool
Texture::Format::operator == ( Texture::FormatValue value ) const
{
	return GetValue() == value;
}

Texture::Format
Texture::Format::NonCore( U16 index, U16 layoutDetails )
{
	Format format;
	
	format.fValue = Display::MixPartWithLayoutDetails( kNonCore, layoutDetails );
	format.fIndex = index;
	
	return format;
}

Texture::FormatValue
Texture::Format::GetValue( U16* index, U16* layoutDetails ) const
{
	if (layoutDetails)
	{
		*layoutDetails = Display::ExtractLayoutDetails( fValue );
	}
	
	return (FormatValue)Display::GetValueAndIndex( fValue, fIndex, FormatValue::kNonCore, FormatValue::kRGBA, index );
}

// ----------------------------------------------------------------------------

Texture::Texture( Rtt_Allocator* allocator )
:	Super( allocator ),
	fMipmapMagFilter( kLinear ),
	fMipmapMinFilter( kLinear ),
	fIsRetina( false ),
	fIsTarget( false ),
	fGenMipmaps( false ),
	fHasCustomUploader( false )
{
}

Texture::~Texture()
{
}

CPUResource::ResourceType 
Texture::GetType() const
{
	return CPUResource::kTexture;
}

void 
Texture::Allocate()
{
}

void 
Texture::Deallocate()
{
}

Texture::Wrap
Texture::GetWrapX() const
{
	return kClampToEdge;
}

Texture::Wrap
Texture::GetWrapY() const
{
	return kClampToEdge;
}

size_t 
Texture::GetSizeInBytes() const
{
	Format format = GetFormat();
	U32 w = GetWidth();
	U32 h = GetHeight();

	U16 layoutDetails;
	switch(format.GetValue( NULL, &layoutDetails ))
	{
		case kLuminance:	return w * h * 1;
		case kRGB:			return w * h * 3;
		case kRGBA:			return w * h * 4;
		case kBGRA:			return w * h * 4;
		case kABGR:			return w * h * 4;
		case kARGB:			return w * h * 4;
		case kNonCore:
		{
			NonCoreFormatInfo info = Display::DecodeLayoutDetails( layoutDetails );
			
			if ( 0 == info.fBlockSize ) // not compressed?
			{
				return w * h * info.fBytesPerComponent * info.fNumComponents;
			}
			else
			{
				return Format::GetCompressedSize( w, h, info.fBlockWidth, info.fBlockHeight, info.fBlockSize );
			}
		}
		default:			return 0;
	}
}

U8
Texture::GetByteAlignment() const
{
	return 4;
}

void
Texture::DoCustomUpload( void* resource, CustomUploadTextureInfo& info ) const
{
	Rtt_ASSERT_NOT_REACHED();
}

const U8*
Texture::GetData() const
{
	return NULL;
}

void
Texture::ReleaseData()
{
}

void
Texture::SetFilter( Filter newValue )
{
	// Must implement in derived class
	Rtt_ASSERT_NOT_REACHED();
}

void
Texture::SetWrapX( Wrap newValue )
{
	// Must implement in derived class
	Rtt_ASSERT_NOT_REACHED();
}

void
Texture::SetWrapY( Wrap newValue )
{
	// Must implement in derived class
	Rtt_ASSERT_NOT_REACHED();
}

#define PACK_ASTC( WIDTH, HEIGHT ) ( ( WIDTH << 4 ) | ( HEIGHT ) )

static const U16 kASTCDims[] = {
	PACK_ASTC( 4, 4 ),
	PACK_ASTC( 5, 4 ),
	PACK_ASTC( 5, 5 ),
	PACK_ASTC( 6, 5 ),
	PACK_ASTC( 6, 6 ),
	PACK_ASTC( 8, 5 ),
	PACK_ASTC( 8, 6 ), 
	PACK_ASTC( 8, 8 ),
	PACK_ASTC( 10, 5 ), 
	PACK_ASTC( 10, 6 ), 
	PACK_ASTC( 10, 8 ), 
	PACK_ASTC( 10, 10 ),
	PACK_ASTC( 12, 10 ),
	PACK_ASTC( 12, 12 )
};

int
Texture::Format::BlockDimsID( U8 width, U8 height )
{
	Rtt_ASSERT( width < 16 );
	Rtt_ASSERT( height < 16 );
	
	U8 packed = PACK_ASTC( width, height );
	for ( int i = 0; i < sizeof( kASTCDims ) / sizeof( *kASTCDims ); i++ )
	{
		if ( kASTCDims[i] == packed )
		{
			return i;
		}
	}
	
	Rtt_ASSERT_NOT_REACHED();

	return -1;
}

void
Texture::Format::GetBlockDims( int blockDimsID, U8& width, U8& height )
{
	Rtt_ASSERT( blockDimsID >= 0 );
	Rtt_ASSERT( blockDimsID < sizeof( kASTCDims ) / sizeof( *kASTCDims ) );
	
	U8 packed = kASTCDims[blockDimsID];

	width = packed >> 4;
	height = packed & 0xF;
}
		
#undef PACK_ASTC
		
template<int N> inline int
RoundUpNPOT( U8 dim )
{
	return ( dim + N - 1 ) / N;
}

static int RoundUpToMultiple( U8 dim, U8 size )
{
	switch (size)
	{
		case 4: // most cases
			return ( dim + 3 ) / 4;
		case 8: // ASTC...
			return ( dim + 7 ) / 8;
		case 3: // ...and ditto the rest, albeit not powers of 2
			return RoundUpNPOT<3>( dim );
		case 5:
			return RoundUpNPOT<5>( dim );
		case 6:
			return RoundUpNPOT<6>( dim );
		case 10:
			return RoundUpNPOT<10>( dim );
		case 12:
			return RoundUpNPOT<12>( dim );
		default:
			Rtt_ASSERT_NOT_REACHED();
		
			return 0;
	}
}
			
int
Texture::Format::GetCompressedSize( U8 w, U8 h, U8 blockWidth, U8 blockHeight, U8 blockSize )
{
	int blocksW = RoundUpToMultiple( w, blockWidth );
	int blocksH = RoundUpToMultiple( h, blockHeight );
	
	return blocksW * blocksH * blockSize;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
