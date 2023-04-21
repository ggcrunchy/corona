//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#ifndef _Rtt_WuffsLoader_H__
#define _Rtt_WuffsLoader_H__

#include "Core/Rtt_Types.h"
#include "..\..\external\wuffs\wuffs.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

class WuffsLoader
{
	public:
		WuffsLoader();

	public:
		void SetSource( const void* data, U32 size );

	public:
		static void DeleteLoaderElseFreeData( WuffsLoader* loader, void* data );

	public:
		U32 GetWidth() const;
		U32 GetHeight() const;

		U8* GetData() const { return fData; }

	private:
		void Reset();

	private:
		wuffs_aux::MemOwner fMemOwner;
		wuffs_base__pixel_buffer fPixbuf;
		U8* fData;
};
	
// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

#endif // _Rtt_WuffsLoader_H__