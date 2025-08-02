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
	fIsRetina( false ),
	fIsTarget( false )
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
			
			return w * h * info.fBytesPerComponent * info.fNumComponents;
		}
		default:			return 0;
	}
}

U8
Texture::GetByteAlignment() const
{
	return 4;
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

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
