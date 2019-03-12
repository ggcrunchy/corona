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
#include "Core/Rtt_SharedPtr.h"
#include "Corona/CoronaLua.h"
#include "Display/Rtt_UniformArrayAdapter.h"
#include "Renderer/Rtt_Program.h"
#include "Renderer/Rtt_UniformArray.h"
#include "Rtt_Lua.h"
#include "Rtt_LuaUserdataProxy.h"

#include <string.h>

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

UniformArray::UniformArray( Rtt_Allocator *allocator, U32 count )
:	CPUResource( allocator ),
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

static const char kUniformArrayMT[] = "UniformArray";

void
UniformArray::Register( lua_State *L )
{
	luaL_getmetatable( L, kUniformArrayMT ); /* ..., mt */

	if (lua_isnil( L, -1 ))
	{
		lua_pop( L, 1 );

		Lua::NewGCMetatable( L, kUniformArrayMT, []( lua_State *L ) {
			Rtt_DELETE( (SharedPtr<UniformArray> *)Lua::ToUserdata( L, 1, kUniformArrayMT ) );

			return 0;
		});
	}

	lua_pushlightuserdata( L, this ); /* ..., mt, raw ptr */

	SharedPtr<UniformArray> * ptr = (SharedPtr<UniformArray> *)lua_newuserdata( L, sizeof(SharedPtr<UniformArray>) );

	new (ptr) SharedPtr<UniformArray>( this ); /* ..., mt, raw ptr, shared ptr */

	lua_pushvalue( L, -3 ); /* ..., mt, raw ptr, shared ptr, mt */
	lua_setmetatable( L, -2 ); /* ..., mt, raw ptr, shared ptr; shared ptr.metatable = mt */
	lua_rawset( L, -3 ); /* ..., mt = { .., [raw ptr] = shared ptr } */
	lua_pop( L, 1 ); /* ... */
}

void
UniformArray::Release( lua_State *L )
{
	luaL_getmetatable( L, kUniformArrayMT );

	if (!lua_isnil( L, -1 ))
	{
		lua_pushlightuserdata( L, this );
		lua_pushnil( L );
		lua_rawset( L, -3 );
	}

	lua_pop( L, 1 );
}

bool
UniformArray::IsRegistered( lua_State *L, int arrayIndex )
{
	int top = lua_gettop( L );

	arrayIndex = CoronaLuaNormalize( L, arrayIndex );

	luaL_getmetatable( L, kUniformArrayMT );

	bool registered = false;

	if (!lua_isnil( L, -1 ))
	{
		for (lua_pushnil( L ); lua_next( L, -2 ); lua_pop( L, 1 ))
		{
			if (!lua_islightuserdata( L, -2 )) // ignore __gc
			{
				continue;
			}

			UniformArray *uniformArray = (UniformArray *)lua_touserdata( L, -2 );

			uniformArray->PushProxy( L );

			if (lua_equal( L, -1, arrayIndex ))
			{
				registered = true;

				break;
			}

			lua_pop( L, 1 );
		}
	}

	lua_settop( L, top );

	if (!registered)
	{
		CoronaLuaError( L, "Object at index %i is either not an array of uniforms or has been released" );
	}

	return registered;
}

class VersionedObserver : public UniformArrayState
{
public:
	VersionedObserver()
	{
		for (U32 i = 0; i < (U32)Program::kNumVersions; ++i)
		{
			fTimestamps[i] = 0U;
		}
	}

	virtual U32 GetTimestamp( Program::Version version ) const
	{
		return fTimestamps[version];
	}

	virtual void SetTimestamp( Program::Version version, U32 timestamp )
	{
		fTimestamps[version] = timestamp;
	}

private:
	U32 fTimestamps[Program::kNumVersions];
};

UniformArrayState *
UniformArray::NewObserverState() const
{
	// for now:
		return Rtt_NEW( GetAllocator(), VersionedObserver() );
	// but if using a UBO would have single (shared?) timestamp
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------
