//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "Rtt_BufferBitmap.h"
#include "Rtt_Display.h" // GLES query

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

// TODO: Does TextureBitmap replace the need for this???
// 
BufferBitmap::BufferBitmap( Rtt_Allocator* allocator, size_t w, size_t h, PlatformBitmap::Format format, Orientation orientation )
:	fData( Rtt_MALLOC( allocator, w*h*PlatformBitmap::BytesPerPixel( format ) ) ),
	fWidth( (U32) w ),
	fHeight( (U32) h ),
	fProperties( 0 ),
	fFormat( format ),
	fOrientation( orientation )
{
	Rtt_ASSERT( fData );
}

BufferBitmap::~BufferBitmap()
{
	ReleaseBits();
}

void
BufferBitmap::ReleaseBits()
{
	if ( fData )
	{
		Rtt_FREE( fData );
		fData = NULL;
	}
}

const void*
BufferBitmap::GetBits( Rtt_Allocator* ) const
{
	return fData;
}

void
BufferBitmap::FreeBits() const
{
	// We don't free bits b/c if we do, we have no way to recover the data
}

U32
BufferBitmap::Width() const
{
	// No need to handle auto-rotate case b/c we don't allow you to set the auto-rotate property
	return fWidth;
}

U32
BufferBitmap::Height() const
{
	// No need to handle auto-rotate case b/c we don't allow you to set the auto-rotate property
	return fHeight;
}

U32
BufferBitmap::UprightWidth() const
{
	S32 angle = DegreesToUpright();
	return ( 0 == angle || 180 == Abs( angle ) ) ? fWidth : fHeight;
}

U32
BufferBitmap::UprightHeight() const
{
	S32 angle = DegreesToUpright();
	return ( 0 == angle || 180 == Abs( angle ) ) ? fHeight : fWidth;
}

PlatformBitmap::Format
BufferBitmap::GetFormat() const
{
	return (PlatformBitmap::Format)fFormat;
}

bool
BufferBitmap::IsProperty( PropertyMask mask ) const
{
	return (fProperties & mask) ? true : false;
}

void
BufferBitmap::SetProperty( PropertyMask mask, bool newValue )
{
	const U8 p = fProperties;
	const U8 propertyMask = (U8)mask;
	fProperties = ( newValue ? p | propertyMask : p & ~propertyMask );
}

PlatformBitmap::Orientation
BufferBitmap::GetOrientation() const
{
	return (Orientation)fOrientation;
}

void
BufferBitmap::Flip( bool flipHorizontally, bool flipVertically )
{
	int bytesPerPixel;
	int xIndexMax;
	int yIndexMax;
	int xIndex;
	int yIndex;

	// Get the halfway point in the bitmap according to which direction we're flipping the image.
	// If we don't do this, then we'll re-flip the image when scanning the last half of the bitmap.
	if (flipHorizontally && flipVertically)
	{
		xIndexMax = Width() - 1;
		yIndexMax = Height() / 2;
	}
	else if (flipHorizontally)
	{
		xIndexMax = Width() / 2;
		yIndexMax = Height() - 1;
	}
	else if (flipVertically)
	{
		xIndexMax = Width() - 1;
		yIndexMax = Height() / 2;
	}
	else
	{
		// Exit out since we're not flipping in either direction.
		return;
	}

	// Swap pixels in the bitmap buffer.
	bytesPerPixel = (int)PlatformBitmap::BytesPerPixel(GetFormat());
	for (yIndex = 0; yIndex <= yIndexMax; yIndex++)
	{
		for (xIndex = 0; xIndex <= xIndexMax; xIndex++)
		{
			// Get the pixels to swap.
			int xSwapIndex = xIndex;
			int ySwapIndex = yIndex;
			if (flipHorizontally)
			{
				xSwapIndex = ((int)Width() - 1) - xSwapIndex;
			}
			if (flipVertically)
			{
				ySwapIndex = ((int)Height() - 1) - ySwapIndex;
			}
			int swapPixelIndex = (xSwapIndex + (ySwapIndex * (int)Width())) * bytesPerPixel;
			int currentPixelIndex = (xIndex + (yIndex * (int)Width())) * bytesPerPixel;

			// Stop here if we've reached the halfway point of the bitmap.
			if (currentPixelIndex >= swapPixelIndex)
			{
				break;
			}

			// Swap pixels one color channel at a time.
			for (int channelIndex = 0; channelIndex < (int)bytesPerPixel; channelIndex++)
			{
				U8 *currentPixelChannel = ((U8*)fData) + currentPixelIndex + channelIndex;
				U8 *swapPixelChannel = ((U8*)fData) + swapPixelIndex + channelIndex;
				U8 value = *currentPixelChannel;
				*currentPixelChannel = *swapPixelChannel;
				*swapPixelChannel = value;
			}
		}
	}
}

template<
	int kRedIndex,
	int kGreenIndex,
	int kBlueIndex,
	int kAlphaIndex
>
void DoPixel( U32 *p )
{
	U8 a = ((U8 *)p)[kAlphaIndex];

	if ( a > 0 )
	{
		U8& r = ((U8 *)p)[kRedIndex];
		U8& g = ((U8 *)p)[kGreenIndex];
		U8& b = ((U8 *)p)[kBlueIndex];

		float invAlpha = 255.f / (float)a;
		r = invAlpha * r;
		g = invAlpha * g;
		b = invAlpha * b;
	}
}

void
BufferBitmap::UndoPremultipliedAlpha()
{
	// We're assuming 4 bytes (U32) per pixel.
	U32 *p = static_cast< U32 * >( WriteAccess() );

	Rtt_ASSERT( sizeof( *p ) == PlatformBitmap::BytesPerPixel( GetFormat() ) );

	int numPixels = ( Width() * Height() );
#if 0
	for( int i = 0;
			i < numPixels;
			++i )
	{
		#ifdef Rtt_OPENGLES
			//RGBA
			U8 a = ((U8 *)p)[3];
		#else
			//ARGB
			U8 a = ((U8 *)p)[0];
		#endif

		if ( a > 0 )
		{
			#ifdef Rtt_OPENGLES
				//RGBA
				U8& r = ((U8 *)p)[0];
				U8& g = ((U8 *)p)[1];
				U8& b = ((U8 *)p)[2];
			#else
				//ARGB
				U8& r = ((U8 *)p)[1];
				U8& g = ((U8 *)p)[2];
				U8& b = ((U8 *)p)[3];
			#endif

			float invAlpha = 255.f / (float)a;
			r = invAlpha * r;
			g = invAlpha * g;
			b = invAlpha * b;
		}

		p++;
	}
#endif
	if ( Display::IsUsingGLES() )
	{
		for( int i = 0;
				i < numPixels;
				++i )
		{
			DoPixel<
				0, 1, 2, /* RGB */
				3 /* Alpha */
			>( p );
		
			p++;
		}
	}
	
	else
	{
		for( int i = 0;
				i < numPixels;
				++i )
		{
			DoPixel<
				1, 2, 3, /* RGB */
				0 /* Alpha */
			>( p );
		
			p++;
		}
	}
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
