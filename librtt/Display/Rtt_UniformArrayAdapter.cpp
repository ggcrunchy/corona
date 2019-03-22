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

#include "Core/Rtt_Macros.h"
#include "Core/Rtt_Types.h"
#include "Core/Rtt_Math.h"
#include "Core/Rtt_Real.h"
#include "Display/Rtt_UniformArrayAdapter.h"
#include "Renderer/Rtt_UniformArray.h"
#include "Rtt_LuaContext.h"

#include "Corona/CoronaLog.h"

// ----------------------------------------------------------------------------

namespace Rtt
{

// ----------------------------------------------------------------------------

const UniformArrayAdapter&
UniformArrayAdapter::Constant()
{
	static const UniformArrayAdapter sAdapter;
	return sAdapter;
}

// ----------------------------------------------------------------------------
	
StringHash *
UniformArrayAdapter::GetHash( lua_State *L ) const
{
	static const char *keys[] = 
	{
		"newUniformsSetter",// 0
		"numVectors",		// 1
		"releaseSelf",		// 2
		"setUniforms",		// 3
	};

	static StringHash sHash( *LuaContext::GetAllocator( L ), keys, sizeof( keys ) / sizeof( const char * ), 4, 0, 2, __FILE__, __LINE__ );
	return &sHash;
}

int
UniformArrayAdapter::ValueForKey(
			const LuaUserdataProxy& sender,
			lua_State *L,
			const char *key ) const
{
	int result = 0;

	Rtt_ASSERT( key ); // Caller should check at the top-most level

	const UniformArray *uniformArray = (const UniformArray *)sender.GetUserdata();
	if ( ! uniformArray ) { return result; }

	int index = GetHash( L )->Lookup( key );

	result = 1;

	switch ( index )
	{
		case 0:
			{
				lua_pushinteger( L, uniformArray->GetSizeInVectors() );
			}
			break;
		case 1:
			Lua::PushCachedFunction( L, newUniformsSetter );
			break;
		case 2:
			Lua::PushCachedFunction( L, releaseSelf );
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
UniformArrayAdapter::SetValueForKey(
			LuaUserdataProxy& sender,
			lua_State *L,
			const char *key,
			int valueIndex ) const
{
	bool result = false;

	Rtt_ASSERT( key ); // Caller should check at the top-most level

	UniformArray *uniformArray = (UniformArray *)sender.GetUserdata();
	if ( ! uniformArray ) { return result; }
	/*
	int index = GetHash( L )->Lookup( key );

	switch ( index )
	{
	// anything to do?
		default:
			break;
	}
	*/
	return result;
}

void
UniformArrayAdapter::WillFinalize( LuaUserdataProxy& sender ) const
{
	UniformArray *state = (UniformArray *)sender.GetUserdata();
	if ( ! state ) { return; }
	
	state->DetachProxy();
}

int
UniformArrayAdapter::newUniformsSetter( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	UniformArray *uniformArray = (UniformArray *)sender->GetUserdata();
	if ( ! uniformArray ) { return result; }

	// TODO: needs DisplayObject-ish object
	// when "drawn", will update contents of uniform array
	// this is the reason for all the hassle with memory management in uniform arrays' commands

	return 0; // NYI
}

int
UniformArrayAdapter::releaseSelf( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	UniformArray *uniformArray = (UniformArray *)sender->GetUserdata();
	if ( ! uniformArray ) { return result; }

	sender->DetachUserdata();

	return 0;
}
#if 0
// THIS BELONGS SOMEWHERE ELSE
int
ShaderStateAdapter::releaseEffect( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	ShaderState *state = (ShaderState *)sender->GetUserdata();
	if ( ! state ) { return result; }

	std::string name = state->GetShaderName();

	if (!name.empty())
	{
		// TODO: tell factory to evict it

		state->SetShaderName( "" );
	}

	else
	{
		const char *reason = state->GetShaderCategory() != ShaderTypes::kCategoryDefault ?
							 "has already been released" :
							 "does not have release privileges";

		CORONA_LOG_WARNING( "releaseEffect: Unable to release, effect %s", reason );
	}

	return 0;
}
#endif

static U32
GetComponentCount( Uniform::DataType type )
{
	U32 comps = 0U;

	switch (type)
	{
	case Uniform::kScalar:
		comps = 1U;
		break;
	case Uniform::kVec2:
		comps = 2U;
		break;
	case Uniform::kVec3:
		comps = 3U;
		break;
	case Uniform::kVec4:
		comps = 4U;
		break;
	case Uniform::kMat3:
		comps = 9U;
		break;
	case Uniform::kMat4:
		comps = 16U;
		break;
	default:
		Rtt_ASSERT_NOT_REACHED();
	}

	return comps;
}

static void
ZeroPad( Real uniform[], U32 i, U32 comps )
{
	while (i < comps)
	{
		uniform[i++] = Rtt_REAL_0;
	}
}

static U32
ReadUniform( lua_State *L, int tableIndex, Real uniform[], U32 read_pos, U32 comps )
{
	U32 nread = 0U;

	for (U32 i = 0U; i < comps; ++i)
	{
		lua_rawgeti( L, tableIndex, ++read_pos );

		if (!lua_isnil( L, -1 ))
		{
			uniform[nread++] = luaL_toreal( L, -1 );
		}

		else
		{
			if (nread)
			{
				ZeroPad( uniform, nread, comps );
			}

			comps = 0U;	// kill the loop
		}

		lua_pop( L, 1 );
	}

	return nread;
}

int
UniformArrayAdapter::setUniforms( lua_State *L )
{
	int result = 0;
	int nextArg = 1;
	LuaUserdataProxy* sender = LuaUserdataProxy::ToProxy( L, nextArg++ );
	if(!sender) { return result; }

	UniformArray *uniformArray = (UniformArray *)sender->GetUserdata();
	if ( ! uniformArray ) { return result; }

	int first = 1;

	if (lua_isnumber( L, nextArg ))
	{
		first = lua_tointeger( L, nextArg++ );

		luaL_argcheck( L, first > 0, nextArg - 1, "Index must be positive" );
	}

	int tableIndex = nextArg++;

	luaL_checktype( L, tableIndex, LUA_TTABLE );
	
	Uniform::DataType type = uniformArray->GetDataType();
	U32 comps = GetComponentCount( type ), step = comps;
	bool compact = uniformArray->GetIsCompact();

	if (!compact)
	{
		step += 3U;
		step &= ~3U;
	}

	int nsteps = (int)(uniformArray->GetSizeInVectors() / step);
	int iMin = 1, iMax = nsteps;

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

	U32 read_pos = (U32)(iMin - 1) * comps;
	U32 write_pos = (U32)(first - 1) * step;

	for (nsteps = Min( iMax, nsteps ) - iMin + 1; nsteps > 0; --nsteps, write_pos += step)
	{
		Real uniform[16];

		U32 nread = ReadUniform( L, tableIndex, uniform, read_pos, comps );

		if (nread)
		{
			if (type != Uniform::kMat3 || compact)
			{
				uniformArray->Set( uniform, write_pos, comps );

				if (type != Uniform::kVec4 && type != Uniform::kMat4)
				{
					uniformArray->ZeroPadExtrema();
				}
			}

			else
			{
				uniformArray->Set( &uniform[0], write_pos + 0U, 3U );
				uniformArray->ZeroPadExtrema();
				uniformArray->Set( &uniform[3], write_pos + 4U, 3U );
				uniformArray->ZeroPadExtrema();
				uniformArray->Set( &uniform[6], write_pos + 8U, 3U );
				uniformArray->ZeroPadExtrema();
			}

			read_pos += nread;
		}
		
		if (nread < comps)
		{
			break;
		}
	}

	return 0;
}

// ----------------------------------------------------------------------------

} // namespace Rtt

// ----------------------------------------------------------------------------

