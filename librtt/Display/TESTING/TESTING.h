//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaObjectsTesting_H__
#define _CoronaObjectsTesting_H__

#include "Corona/CoronaLua.h"
#include "Corona/CoronaMacros.h"
#include "Corona/CoronaPublicTypes.h"
#include "Corona/CoronaGraphics.h"
#include "Corona/CoronaObjects.h"
#include "Core/Rtt_Assert.h"
#include "Core/Rtt_Types.h"
#include <string>

extern "C" {
    int ColorMaskLib( lua_State * L );
    int DepthLib( lua_State * L );
    int InstancingLib( lua_State * L );
    int StencilLib( lua_State * L );
    int SupportLib( lua_State * L );
}

void EarlyOutPredicate( const CoronaDisplayObjectHandle, void *, int * result );
void AddToParamsList( CoronaObjectParamsHeader & head, CoronaObjectParamsHeader * params, unsigned short method );
void DisableCullAndHitTest( CoronaObjectParamsHeader & head );
void CopyWriter( U8 * out, const void * data, U32 size );
void DummyWriter( U8 *, const void *, U32 );
void DummyArgs( lua_State * L );
int FindName( lua_State * L, int valueIndex, const char * list[] );
bool FindFunc( lua_State * L, int valueIndex, int * func );

U32 FindAndReplace( std::string & str, const char * original, const char * replacement, bool doMultiple = false );
U32 FindAndInsertAfter( std::string & str, const char * what, const char * toInsert, bool doMultiple = false );

struct ScopeMessagePayload {
	CoronaRendererHandle rendererHandle;
	U32 drawSessionID;
};

template<typename T> struct Boxed {
	T * object;
	bool isNew;
};

template<typename T> Boxed< T >
GetOrNew( lua_State * L, void * nonce, bool construct = false )
{
	Boxed< T > boxed;

	lua_pushlightuserdata( L, nonce ); // ..., nonce
	lua_rawget( L, LUA_REGISTRYINDEX ); // ..., object?

	if (!lua_isnil( L, -1 ))
	{
		boxed.object = (T *)lua_touserdata( L, -1 );
		boxed.isNew = false;
	}

	else
	{
		lua_pushlightuserdata( L, nonce ); // ..., nil, nonce

		boxed.object = (T *)lua_newuserdata( L, sizeof( T ) ); // ..., nil, nonce, object
		boxed.isNew = true;

		if (construct)
		{
			new ( boxed.object ) T;
		}

		else
		{
			memset( boxed.object, 0, sizeof( T ) );
		}

		lua_rawset( L, LUA_REGISTRYINDEX ); // ..., nil; registry = { ..., [nonce] = object }
	}

	lua_pop( L, 1 ); // ...

	return boxed;
}

#endif // _CoronaObjectsTesting_H__
