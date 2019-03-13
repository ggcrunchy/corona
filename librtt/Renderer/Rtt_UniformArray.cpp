//////////////////////////////////////////////////////////////////////////////
//
// Copyright (C) 2018 Corona Labs Inc.
// Contact: support@coronalabs.com
//
// This file is part of the Corona game engine.
//
// Commercial License Usage
// Licensees holding valid commercial Corona licenses may use this file in
// accordance with the commercial license agreement between you and 
// Corona Labs Inc. For licensing terms and conditions please contact
// support@coronalabs.com or visit https://coronalabs.com/com-license
//
// GNU General Public License Usage
// Alternatively, this file may be used under the terms of the GNU General
// Public license version 3. The license is as published by the Free Software
// Foundation and appearing in the file LICENSE.GPL3 included in the packaging
// of this file. Please review the following information to ensure the GNU 
// General Public License requirements will
// be met: https://www.gnu.org/licenses/gpl-3.0.html
//
// For overview and more information on licensing please refer to README.md
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Allocator.h"
#include "Core/Rtt_Assert.h"
#include "Corona/CoronaLua.h"
#include "Display/Rtt_Display.h"
#include "Display/Rtt_ShaderFactory.h"
#include "Display/Rtt_UniformArrayAdapter.h"
#include "Renderer/Rtt_UniformArray.h"
#include "Rtt_LuaUserdataProxy.h"

#include <string.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

UniformArray::UniformArray( Display &display, U32 count )
:	CPUResource( display.GetAllocator() ),
	fDisplay( display ),
	fData( NULL ),
	fSize( count * sizeof( Real ) ),
	fTimestamp( 0 )
{
	Allocate();
	SetDirty( false );

	fLifetimeMaxDirtyOffset = 0U;
	fLifetimeMinDirtyOffset = fSize;
}

UniformArray::~UniformArray()
{
	Deallocate();
}

CPUResource::ResourceType
UniformArray::GetType() const
{
	return CPUResource::kUniformArray;
}

void
UniformArray::Allocate()
{
	fData = new U8[fSize];
}

void
UniformArray::Deallocate()
{
	delete [] fData;

	fData = NULL;
}

void
UniformArray::PushProxy( lua_State *L ) const
{
	if ( ! fProxy )
	{
		fProxy = LuaUserdataProxy::New( L, const_cast< Self * >( this ) );
		fProxy->SetAdapter( & UniformArrayAdapter::Constant() );
	}

	fProxy->Push( L );
}

void
UniformArray::DetachProxy()
{
	if (fProxy)
	{
		fDisplay.GetShaderFactory().ReleaseUniformArray( this );
	//	fProxy->DetachUserdata(); is this needed?
	}

	fProxy = NULL;
}

U32
UniformArray::Set( const U8 *bytes, U32 offset, U32 n )
{
	Rtt_ASSERT( fData );

	if (offset + n > fSize)
	{
		n = offset < fSize ? fSize - offset : 0U;
	}

	if (n)
	{
		memcpy( fData + offset, bytes, n );

		if (!GetDirty())
		{
			++fTimestamp;

			SetDirty( true );
		}

		if (offset < fMinDirtyOffset)
		{
			fMinDirtyOffset = offset;

			if (offset < fLifetimeMinDirtyOffset)
			{
				fLifetimeMinDirtyOffset = offset;
			}
		}

		U32 extent = offset + n;

		if (extent > fMaxDirtyOffset)
		{
			fMaxDirtyOffset = extent;

			if (extent > fLifetimeMaxDirtyOffset)
			{
				fLifetimeMaxDirtyOffset = extent;
			}
		}
	}

	return n;
}

U32
UniformArray::Set( const Real *reals, U32 offset, U32 n )
{
	return Set( reinterpret_cast<const U8 *>( reals ), offset * sizeof( Real ), n * sizeof( Real ) );
}

void
UniformArray::SetDirty( bool newValue )
{
	 fDirty = newValue;

	 if (!fDirty)
	 {
		 fMaxDirtyOffset = 0U;
		 fMinDirtyOffset = fSize;
	 }
}

std::string
UniformArray::Key() const
{
	char key[32];

	sprintf( key, "Array: %p", this );

	return key;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
