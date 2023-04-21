//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////



#define WUFFS_IMPLEMENTATION

#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__AUX__BASE
#define WUFFS_CONFIG__MODULE__AUX__IMAGE
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__BMP
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__WBMP
#define WUFFS_CONFIG__MODULE__ZLIB

#include "Rtt_WuffsLoader.h"
#include "Core/Rtt_Allocator.h"
#include <utility>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

WuffsLoader::WuffsLoader()
:	fMemOwner( nullptr, &free ),
	fPixbuf( wuffs_base__null_pixel_buffer() )
{
	fMemOwner.reset();
}

class Wuffs_Load_RW_Callbacks : public wuffs_aux::DecodeImageCallbacks
{
	public:
		Wuffs_Load_RW_Callbacks( U32 format )
		:	mFormat( format )
		{
		}

	private:
		wuffs_base__pixel_format SelectPixfmt( const wuffs_base__image_config& ) override
		{
			return wuffs_base__make_pixel_format( mFormat );
		}

		U32 mFormat;
};

void WuffsLoader::SetSource( const void* data, U32 size )
{
	Reset();

	Wuffs_Load_RW_Callbacks callbacks( WUFFS_BASE__PIXEL_FORMAT__BGRA_PREMUL );

	wuffs_aux::sync_io::MemoryInput input( static_cast<const U8*>( data ), size );
	
	wuffs_aux::DecodeImageResult res = wuffs_aux::DecodeImage( callbacks, input, WUFFS_BASE__PIXEL_BLEND__SRC );

	if ( !res.pixbuf.pixcfg.pixel_format().is_interleaved() )
	{
		return;
	}
	
	wuffs_base__table_u8 tab = res.pixbuf.plane( 0 );
	
	if ( tab.width != tab.stride || NULL == tab.ptr )
	{
		return;
	}

	fMemOwner = std::move( res.pixbuf_mem_owner );
	fPixbuf = res.pixbuf;
	fData = tab.ptr;
}

void WuffsLoader::DeleteLoaderElseFreeData( WuffsLoader* loader, void* data )
{
	if ( loader )
	{
		Rtt_DELETE( loader );
	}

	else
	{
		Rtt_FREE( data ); // data not owned by loader
	}
}

U32 WuffsLoader::GetWidth() const
{
	return fData ? fPixbuf.pixcfg.width() : 0;
}

U32 WuffsLoader::GetHeight() const
{
	return fData ? fPixbuf.pixcfg.height() : 0;
}

void WuffsLoader::Reset()
{
	fMemOwner.reset();

	fData = NULL;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------