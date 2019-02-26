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

#include "Display/Rtt_ShaderState.h"
#include "Display/Rtt_ShaderStateAdapter.h"
#include "Renderer/Rtt_UniformArray.h"
#include "Rtt_LuaContext.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

const ShaderStateAdapter&
ShaderStateAdapter::Constant()
{
	static const ShaderStateAdapter sAdapter;
	return sAdapter;
}

// ----------------------------------------------------------------------------
	
StringHash *
ShaderStateAdapter::GetHash( lua_State *L ) const
{
	static const char *keys[] = 
	{
		"getUniformsCount",	// 0
		"newUniformsSetter",// 1
		"releaseEffect",	// 2
		"setUniforms",		// 3
	};

	static StringHash sHash( *LuaContext::GetAllocator( L ), keys, sizeof( keys ) / sizeof( const char * ), 1, 1, 1, __FILE__, __LINE__ );
	return &sHash;
}

int
ShaderStateAdapter::ValueForKey(
			const LuaUserdataProxy& sender,
			lua_State *L,
			const char *key ) const
{
	int result = 0;

	Rtt_ASSERT( key ); // Caller should check at the top-most level

	const ShaderState *state = (const ShaderState *)sender.GetUserdata();
	if ( ! state ) { return result; }

	int index = GetHash( L )->Lookup( key );

	result = 1;

	switch ( index )
	{
		case 0:
			Lua::PushCachedFunction( L, getUniformsCount );
			break;
		case 1:
			Lua::PushCachedFunction( L, newUniformsSetter );
			break;
		case 2:
			Lua::PushCachedFunction( L, releaseEffect );
			break;
		case 3:
			Lua::PushCachedFunction( L, setUniforms );
			break;
		default:
			result = 0;
			break;
	}
	
	return result;
}

bool
ShaderStateAdapter::SetValueForKey(
			LuaUserdataProxy& sender,
			lua_State *L,
			const char *key,
			int valueIndex ) const
{
	bool result = false;

	Rtt_ASSERT( key ); // Caller should check at the top-most level

	ShaderState *state = (ShaderState *)sender.GetUserdata();
	if ( ! state ) { return result; }

	int index = GetHash( L )->Lookup( key );

	switch ( index )
	{
		default:
			break;
	}

	return result;
}

void
ShaderStateAdapter::WillFinalize( LuaUserdataProxy& sender ) const
{
	ShaderState *state = (ShaderState *)sender.GetUserdata();
	if ( ! state ) { return; }
	
	state->DetachProxy();
}

int
ShaderStateAdapter::getUniformsCount( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	ShaderState *state = (ShaderState *)sender->GetUserdata();
	if ( ! state ) { return result; }

	UniformArray *uniformArray = state->GetUniformArray();

	lua_pushinteger( L, uniformArray ? uniformArray->GetSizeInVectors() : 0 );

	return 0;
}

int
ShaderStateAdapter::newUniformsSetter( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	ShaderState *state = (ShaderState *)sender->GetUserdata();
	if ( ! state ) { return result; }

	// TODO: needs DisplayObject-ish object
	// when "drawn", will update contents of uniform array
	// this is the reason for all the hassle with memory management in uniform arrays' commands

	return 0; // NYI
}

int
ShaderStateAdapter::releaseEffect( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	ShaderState *state = (ShaderState *)sender->GetUserdata();
	if ( ! state ) { return result; }

	// TODO: if allowed, search for prototype proxy in registry and evict

	return 0;
}

static void
SetSingleUniformVector( UniformArray *uniformArray, lua_State *L, U32 index )
{
	Real uniform[4];

	for (int i = 0; i < 4; ++i)
	{
		// Errors along the way mean partially written uniforms, but guarding against this
		// requires a potentially expensive verify step or making a copy. As a compromise,
		// treat non-numbers as 0.
		uniform[i] = lua_isnumber( L, -4 + i ) ? (Real)lua_tonumber( L, -4 + i ) : Rtt_REAL_0;
	}

	uniformArray->Set( uniform, index, 1U );
}

int
ShaderStateAdapter::setUniforms( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	ShaderState *state = (ShaderState *)sender->GetUserdata();
	if ( ! state ) { return result; }

	UniformArray *uniformArray = state->GetUniformArray();
	int first = 1;

	if (lua_isnumber( L, nextArg ))
	{
		first = lua_tointeger( L, nextArg++ );

		luaL_argcheck( L, first > 0 && (U32)first < uniformArray->GetSizeInVectors(), nextArg - 1, "Uniform index is out of bounds" );
	}

	int tableIndex = nextArg++;

	luaL_checktype( L, tableIndex, LUA_TTABLE );
	lua_getfield( L, tableIndex, "x" );
	lua_getfield( L, tableIndex, "y" );
	lua_getfield( L, tableIndex, "z" );
	lua_getfield( L, tableIndex, "w" );

	bool single = !lua_isnil( L, -4 ) || !lua_isnil( L, -3 ) || !lua_isnil( L, -2 ) || !lua_isnil( L, -1 );

	if (single)
	{
		SetSingleUniformVector( uniformArray, L, (U32)(first - 1) ); // see note (although since not in a loop, could be more aggressive)
	}

	lua_pop( L, 4 );

	if (!single)
	{
		int iMin = 1, iMax = 0;

		if (lua_isnumber( L, nextArg ))
		{
			iMin = lua_tointeger( L, nextArg++ );

			luaL_argcheck( L, iMin >= 1, nextArg - 1, "Table min index is out of bounds" );

			if (lua_isnumber( L, nextArg ))
			{
				iMax = lua_tointeger( L, nextArg );

				luaL_argcheck( L, iMax >= iMin, nextArg, "Table max index is out of bounds" );
			}
		}

		int offset = (iMin - 1) * 4;

		for (U32 i = (U32)(first - 1), size = uniformArray->GetSizeInVectors(); i < size; ++i, ++iMin, offset += 4)
		{
			lua_rawgeti( L, tableIndex, offset + 1 );
			lua_rawgeti( L, tableIndex, offset + 2 );
			lua_rawgeti( L, tableIndex, offset + 3 );
			lua_rawgeti( L, tableIndex, offset + 4 );

			bool done = iMax ?
				iMin > iMax :
				!lua_isnumber( L, -4 ) || !lua_isnumber( L, -3 ) || !lua_isnumber( L, -2 ) || !lua_isnumber( L, -1 ); // see note in SetSingleUniformVector()

			if (done)
			{
				break;
			}
			
			else
			{
				SetSingleUniformVector( uniformArray, L, i );
			}

			lua_pop( L, 4 );
		}
	}

	return 0;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

