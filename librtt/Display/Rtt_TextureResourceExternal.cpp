//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Display/Rtt_TextureResourceExternal.h"
#include "Rtt_TextureResourceExternalAdapter.h"
#include "Rtt_PlatformBitmapTexture.h"
#include "Rtt_Display.h"
#include "Rtt_TextureFactory.h"
#include "CoronaLua.h"
#include "CoronaGraphics.h"
#include "Rtt_DisplayDefaults.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

#pragma mark ==External Bitmap==

	
class ExternalBitmap : public PlatformBitmap
{
public:
	ExternalBitmap(const Display& display, const CoronaExternalTextureCallbacks* sourceCallbacks, void* context)
	: fDisplay(display),
	  fContext(context)
	{
		if (sizeof(CoronaExternalTextureCallbacks2) == sourceCallbacks->size)
		{
			Rtt_STATIC_ASSERT( offsetof( CoronaExternalTextureCallbacks, size ) ==
							  offsetof( CoronaExternalTextureCallbacks2, base.size ) );
		
			CoronaExternalTextureCallbacks2* extended = (CoronaExternalTextureCallbacks2 *)sourceCallbacks;
			
			fSrc2 = *extended;
		}
		else
		{
			memset( &fSrc2, 0, sizeof( fSrc2 ) );
			
			fSrc = *sourceCallbacks;
		}
	}
	
	void Finalize()
	{
		if ( fSrc.onFinalize )
		{
			fSrc.onFinalize(GetUserData());
		}
		memset(&fSrc, 0, sizeof(fSrc));
	}
	
	~ExternalBitmap()
	{
		Finalize();
	}
	
	virtual const void* GetBits( Rtt_Allocator* context ) const override
	{
		const void *bits = NULL;
		if ( Rtt_VERIFY(fSrc.onRequestBitmap) )
		{
			bits = fSrc.onRequestBitmap(GetUserData());
			if (bits == NULL)
			{
				Rtt_LogException("ERROR: TextureResourceExternal - received null pointer for bitmap data");
			}
		}
		return bits;
	}
	
	virtual void FreeBits() const override
	{
		if( fSrc.onReleaseBitmap)
		{
			return fSrc.onReleaseBitmap(GetUserData());
		}
	}
	
	virtual void DoCustomUpload( void* resource, CustomUploadTextureInfo& info ) const override
	{
		Rtt_ASSERT( fSrc2.customUpload ); // n.b. guarded by HasCustomUploader() check
		Rtt_ASSERT( fSrc2.supplyInternalFormatByValue );
		Rtt_ASSERT( fSrc2.getCustomUploadTextureTarget );
		Rtt_ASSERT( fSrc2.getCustomUploadProvidesMipmaps );
				
		if ( resource ) // not just a query?
		{
			info.fError = fSrc2.customUpload( resource, GetUserData() );
		}
		if ( 0 == info.fError )
		{
		
			info.fTextureTarget = fSrc2.getCustomUploadTextureTarget( GetUserData() );
			info.fTextureFormat = fSrc2.supplyInternalFormatByValue( GetUserData() );
			info.fAddedMipmaps = fSrc2.getCustomUploadProvidesMipmaps( GetUserData() );
		}
	}
	
	virtual U32 Width() const override
	{
		if(fSrc.getWidth)
		{
			return fSrc.getWidth(GetUserData());
		}
		return 0;
	}
	
	virtual U32 Height() const override
	{
		if(fSrc.getHeight)
		{
			return fSrc.getHeight(GetUserData());
		}
		return 0;
	}
	
	virtual Format GetFormat() const override
	{
		CoronaExternalBitmapFormat fmt = kExternalBitmapFormat_Undefined;
		if( fSrc.getFormat )
		{
			fmt = fSrc.getFormat(GetUserData());
		}
		switch(fmt)
		{
			case kExternalBitmapFormat_Mask:
				return PlatformBitmap::kMask;
			case kExternalBitmapFormat_RGB:
				return PlatformBitmap::kRGB;
			case kExternalBitmapFormat_RGBA:
				return PlatformBitmap::kRGBA;
			case kExternalBitmapFormat_RequestedByName:
				if ( fSrc2.getRequestedFormat )
				{
					const char* name = fSrc2.getRequestedFormat(GetUserData());
					U16 formatIndex;
					
					if ( NULL != name && fDisplay.QueryTextureInfo( "Supported", name, &formatIndex ) )
					{
						U16 layoutDetails = fDisplay.EncodeNonCoreFormatLayoutDetails( formatIndex );
						return PlatformBitmap::Format::NonCore( formatIndex, layoutDetails );
					}
				}
			
				if ( fSrc2.supplyInternalFormatByValue )
				{
					U32 internalFormat = fSrc2.supplyInternalFormatByValue(GetUserData());
					U16 formatIndex;
					
					char name[ 1 + sizeof(U32) ] = { Display::kInternalFormatByValueMarker };
					
					memcpy( name + 1, &internalFormat, sizeof(U32) );
					
					if ( fDisplay.QueryTextureInfo( "Supported", name, &formatIndex ))
					{
						U16 layoutDetails = fDisplay.EncodeNonCoreFormatLayoutDetails( formatIndex );
						return PlatformBitmap::Format::NonCore( formatIndex, layoutDetails );
					}
				}
			
				// fallthrough if unresolved
			case kExternalBitmapFormat_Undefined:
				return PlatformBitmap::kRGBA;
		}
		return PlatformBitmap::kRGBA;
	}
	
	int GetField(lua_State* L, const char* field)
	{
		if ( fSrc.onGetField )
		{
			return fSrc.onGetField(L, field, GetUserData());
		}
		return 0;
	}
	
	inline void* GetUserData() const
	{
		return fContext;
	}
	
private:
	const Display& fDisplay;
	union {
		CoronaExternalTextureCallbacks fSrc;
		CoronaExternalTextureCallbacks2 fSrc2;
	};
	void* fContext;
};

	
#pragma mark == Texture Resource External ==

TextureResourceExternal *
TextureResourceExternal::Create(TextureFactory& factory,
									const CoronaExternalTextureCallbacks *callbacks,
									void *callbacksContext,
									bool isRetina )
{	
	Display& display = factory.GetDisplay();
	
	PlatformBitmap *bitmap = Rtt_NEW(display.GetAllocator(),
									ExternalBitmap(display, callbacks, callbacksContext));
	
	bitmap->SetMagFilter( display.GetDefaults().GetMagTextureFilter() );
	bitmap->SetMinFilter( display.GetDefaults().GetMinTextureFilter() );
	bitmap->SetWrapX( display.GetDefaults().GetTextureWrapX() );
	bitmap->SetWrapY( display.GetDefaults().GetTextureWrapY() );
	
	Texture *texture = Rtt_NEW( display.GetAllocator(),
									PlatformBitmapTexture( display.GetAllocator(), *bitmap ) );
	
	TextureResourceExternal *result = Rtt_NEW( display.GetAllocator(),
									TextureResourceExternal( factory, texture, bitmap ) );
	
	texture->SetRetina( isRetina );
	
	if ( sizeof( CoronaExternalTextureCallbacks2 ) == callbacks->size )
	{
		CoronaExternalTextureCallbacks2* callbacks2 = (CoronaExternalTextureCallbacks2*)callbacks;
		if ( NULL != callbacks2->customUpload )
		{
			if ( NULL != callbacks2->supplyInternalFormatByValue &&
				NULL != callbacks2->getCustomUploadTextureTarget &&
				 NULL != callbacks2->getCustomUploadProvidesMipmaps )
			{
				texture->SetHasCustomUploader( true );
			}
			else
			{
				Rtt_LogException( "ERROR: TextureResourceExternal provided custom uploader but missing one or more accompanying handlers" );
			}
		}
	}
	
	return result;
}


TextureResourceExternal::TextureResourceExternal(
											 TextureFactory &factory,
											 Texture *texture,
											 PlatformBitmap *bitmap )
					   : TextureResource(factory, texture, bitmap, kTextureResourceExternal)
{

}

TextureResourceExternal::~TextureResourceExternal()
{
	GetTextureFactory().RemoveFromTeardownList(GetCacheKey());
}

const MLuaUserdataAdapter&
TextureResourceExternal::GetAdapter() const
{
	return TextureResourceExternalAdapter::Constant();
}

int TextureResourceExternal::GetField(lua_State *L, const char *field) const
{
	return ((ExternalBitmap*)GetBitmap())->GetField( L, field);
}

void* TextureResourceExternal::GetUserData() const
{
	return ((ExternalBitmap*)GetBitmap())->GetUserData();
}

	
void TextureResourceExternal::Teardown()
{
	((ExternalBitmap*)GetBitmap())->Finalize();
}
	
	
} // namespace Rtt




