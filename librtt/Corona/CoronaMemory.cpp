//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Config.h"
#include "Core/Rtt_Types.h"
#include "Core/Rtt_Time.h"

#include "CoronaMemory.h"
#include "CoronaLua.h"

#include <functional>

// ----------------------------------------------------------------------------

struct CoronaMemoryData {
	CoronaMemoryData();
	CoronaMemoryData( const CoronaMemoryData & other ) = default;

	const char * fError;
	const void * fBytes;
	unsigned int fByteCount;
    unsigned int fStashHash;
	unsigned int fStrideCount;
	bool fWritable;
};

CoronaMemoryData::CoronaMemoryData()
:   fError( NULL ),
	fBytes( NULL ),
	fByteCount( 0U ),
    fStashHash( 0U ),
	fStrideCount( 0U ),
	fWritable( false )
{
}

// ----------------------------------------------------------------------------

CORONA_API
const char * CoronaMemoryGetLastError( lua_State * L, CoronaMemoryHandle * memoryHandle, int clear )
{
	if (!CoronaMemoryIsValid( L, memoryHandle ))
	{
		return "Invalid memory handle";
	}

	else
	{
		const char * error = memoryHandle->data->fError;

		if (clear)
		{
			memoryHandle->data->fError = NULL; // n.b. any reference still intact
		}

		return error;
	}
}

CORONA_API
int CoronaMemoryIsValid( lua_State * L, const CoronaMemoryHandle * memoryHandle )
{
	if (memoryHandle && memoryHandle->data && memoryHandle->lastKnownStackPosition != 0)
	{
		return 1;
	}

	else
	{
		return 0;
	}
}

static int
FindObjectOnStack( lua_State * L, void * object, bool (*find)( lua_State * L, int index, void * object ) )
{
	for (int i = 1, top = lua_gettop( L ); i <= top; ++i)
	{
		if (find( L, i, object ))
		{
			return i;
		}
	}

	return 0;
}

static bool
FoundObjectAt( lua_State * L, int index, void * object )
{
	return lua_type( L, index ) == LUA_TUSERDATA && lua_topointer( L, index ) == object;
}

CORONA_API
int CoronaMemoryGetPosition( lua_State * L, CoronaMemoryHandle * memoryHandle )
{
	if (!CoronaMemoryIsValid( L, memoryHandle ))
	{
		return 0;
	}

	memoryHandle->data->fError = NULL;

	if (!FoundObjectAt( L, memoryHandle->lastKnownStackPosition, memoryHandle->data ))
	{
		memoryHandle->lastKnownStackPosition = FindObjectOnStack( L, memoryHandle->data, FoundObjectAt );
	}

	return memoryHandle->lastKnownStackPosition;
}

// ----------------------------------------------------------------------------

static CoronaMemoryCallbacks *
AsCallbacks( lua_State * L, int index )
{
	return ( CoronaMemoryCallbacks * )lua_touserdata( L, index );
}

struct CallbackInfo {
	CallbackInfo( lua_State * L )
    :   fName( NULL ),
		fUserData( NULL )
	{
		if (lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "callbacks" ); // ..., t, callbacks
			lua_getfield( L, -2, "name" ); // ..., t, callbacks, name?
			lua_getfield( L, -3, "userData" ); // ..., t, callbacks, name?, userData?

			fCallbacks = AsCallbacks( L, -3 );
			fUserData = lua_touserdata( L, -1 );

			if (lua_isstring( L, -2 ))
            {
                fName = lua_tostring( L, -2 );
            }
            
			lua_pop( L, 3 ); // ..., t
		}

		else
		{
			fCallbacks = AsCallbacks( L, -1 );
		}

		luaL_argcheck( L, -1, fCallbacks->size == sizeof( CoronaMemoryCallbacks ), "Incompatible memory callbacks" );
		lua_pop( L, 1 );	// ...
	}

	CoronaMemoryCallbacks * fCallbacks;
	const char * fName;
	void * fUserData;
};

static void
GetRegistryTable( lua_State * L, void * cookie )
{
	lua_pushlightuserdata( L, cookie ); // ..., cookie
	lua_rawget( L, LUA_REGISTRYINDEX ); // ..., t?

	if (lua_isnil( L, -1 )) // no table yet?
	{
		lua_pop( L, 1 ); // ...
		lua_newtable( L ); // ..., t
		lua_pushlightuserdata( L, cookie ); // ..., t, cookie
		lua_pushvalue( L, -2 ); // ..., t, cookie, t
		lua_rawset( L, LUA_REGISTRYINDEX ); // ..., t; registry[cookie] = t
	}
}

static void
GetMemoryDataCache( lua_State * L )
{
	static int sCacheCookie;

	GetRegistryTable( L, &sCacheCookie ); // ..., cache
}

static CoronaMemoryData *
GetMemoryData( lua_State * L, const CoronaMemoryData & work )
{
	GetMemoryDataCache( L ); // ..., cache

	lua_rawgeti( L, -1, ( int )lua_objlen( L, -1 ) ); // ..., cache, memoryData?
	lua_remove( L, -2 ); // ..., memoryData?

	if (lua_isnil( L, -1 )) // no memory data yet?
	{
		lua_pop( L, 1 ); // ...
        lua_newuserdata( L, sizeof( CoronaMemoryData ) ); // ..., memoryData
		lua_createtable( L, 1, 0 ); // ..., memoryData, env
		lua_setfenv( L, -2 ); // ..., memoryData; data.environment = env
	}

    CoronaMemoryData * memoryData = ( CoronaMemoryData * )lua_touserdata( L, -1 );

	new (memoryData) CoronaMemoryData( work );

	return memoryData;
}

static void
ReplaceWithError( lua_State * L, int objectIndex, int oldTop, const char * defaultError, const char * substr = NULL )
{
	int newTop = lua_gettop( L );

	if (newTop == oldTop || lua_isnil( L, newTop )) // no custom error?
    {
        lua_pushfstring( L, defaultError, substr ); // ..., object, ..., etc., defError
    }
    
	else if (lua_isstring( L, -1 )) // string-type custom error?
	{
		lua_pushfstring( L, defaultError, substr ); // ..., object, ..., etc., error, defError
		lua_insert( L, -2 ); // ..., object, ..., etc., defError, error
		lua_pushliteral( L, ": " ); // ..., object, ..., etc., defError, error, ": "
		lua_insert( L, -2 ); // ..., object, ..., etc., defError, ": ", error
		lua_concat( L, 3 ); // ..., object, ..., etc., defError .. ": " .. error
	}

	lua_replace( L, objectIndex ); // ..., error, ..., etc.
	lua_settop( L, oldTop ); // ..., error, ...
}

static bool
BeforeGetBytes( lua_State * L, int objectIndex, int oldTop, const CallbackInfo & info, const CoronaMemoryExtra * extra )
{
	if (info.fCallbacks->canAcquire && !info.fCallbacks->canAcquire( L, objectIndex, extra )) // ...[, etc., error]
	{
		ReplaceWithError( L, objectIndex, oldTop, "'canAcquire()' callback failed" ); // ...

		return false;
	}

	lua_settop( L, oldTop ); // ...

	return true;
}

static CoronaMemoryHandle
NullHandle( )
{
	return { 0, NULL };
}

static CoronaMemoryHandle
ReplaceWithErrorAndReturnNullHandle( lua_State * L, int objectIndex, int oldTop, const char * defaultError, const char * substr = NULL )
{
	ReplaceWithError( L, objectIndex, oldTop, defaultError, substr );

	return NullHandle( );
}

static void
AddToStash( lua_State * L, CoronaMemoryHandle * memoryHandle )
{
    lua_getfenv( L, memoryHandle->lastKnownStackPosition ); // ..., object, env
    lua_insert( L, -2 ); // ..., env, object
    lua_setfield( L, -2, "stash" ); // ..., env = { ..., stash = object }
    lua_pop( L, 1 ); // ...
}

static CoronaMemoryHandle
GetBytesAndRest( lua_State * L, int objectIndex, int oldTop, const CallbackInfo & info, CoronaMemoryKind kind, const CoronaMemoryExtra * extra )
{
	CoronaMemoryData work;

    // Try to get the bytes.
	work.fBytes = info.fCallbacks->getBytes( L, objectIndex, kind, &work.fByteCount, extra ); // ...[, etc., error]

	if (!work.fBytes)
	{
		return ReplaceWithErrorAndReturnNullHandle( L, objectIndex, oldTop, "'getBytes()' callback failed" );
	}

	lua_settop( L, oldTop ); // ...

    // The bytes might not be one flat array. Try to get any strides.
	if (info.fCallbacks->getStrides && !info.fCallbacks->getStrides( L, objectIndex, &work.fStrideCount, extra )) // ...[, etc. / strides, error]
	{
		return ReplaceWithErrorAndReturnNullHandle( L, objectIndex, oldTop, "'getStrides()' callback failed" ); // ...
	}

	if (work.fStrideCount > 0U)
	{
		if (oldTop + work.fStrideCount > lua_gettop( L ))
		{
			return ReplaceWithErrorAndReturnNullHandle( L, objectIndex, oldTop, "'getStrides()' callback reports more strides than found on stack" ); // ...
		}

		unsigned strideProduct = 1U;

		for (int i = 1; i <= work.fStrideCount; ++i)
		{
			if (!lua_isnumber( L, -i ))
			{
				return ReplaceWithErrorAndReturnNullHandle( L, objectIndex, oldTop, "'getStrides()' callback returned non-numeric stride" ); // ...
			}

			strideProduct *= ( unsigned int )lua_tointeger( L, -i );
		}

		if (strideProduct > work.fByteCount)
		{
			return ReplaceWithErrorAndReturnNullHandle( L, objectIndex, oldTop, "'getStrides()' callback returns stride spanning more bytes than received" ); // ...
		}
	}

    // Replace the object on the stack, stashing it first.
	lua_pushvalue( L, objectIndex ); // ..., object, ...[, etc., strides], object

	CoronaMemoryData * memoryData = GetMemoryData( L, work ); // ..., object, ...[, etc., strides], object, memoryData

	lua_replace( L, objectIndex ); // ..., memoryData, ...[, etc., strides], object

	memoryData->fWritable = kMemoryWrite == kind;

	CoronaMemoryHandle handle = { objectIndex, memoryData };

	AddToStash( L, &handle ); // ..., memoryData, ...[, etc., strides]

    // If there are strides, save them for lookup.
	if (work.fStrideCount > 0U)
	{
		lua_getfenv( L, objectIndex ); // ..., memoryData, ...[, etc.], strides, env

		int before_strides = -(1 + work.fStrideCount);

		for (int i = 1; i <= work.fStrideCount; ++i)
		{
			lua_pushvalue( L, before_strides + i ); // ..., memoryData, ...[, etc.], strides, env, stride
			lua_rawseti( L, -2, i ); // ..., memoryData, ...[, etc.], strides, env = { ..., stride }
		}
	}

    // Restore the stack and supply the result.
	lua_settop( L, oldTop ); // ..., memoryData, ...

	return handle;
}

static CoronaMemoryHandle
Acquire( lua_State * L, int objectIndex, CoronaMemoryKind kind, void * params )
{
	CallbackInfo info( L ); // ...

	CoronaMemoryExtra extra = { params, info.fUserData };
	int oldTop = lua_gettop( L );

	if (BeforeGetBytes( L, objectIndex, oldTop, info, &extra ))
	{
		return GetBytesAndRest( L, objectIndex, oldTop, info, kind, &extra );
	}

	else
	{
		return NullHandle( );
	}
}

static void
GetWeakTable( lua_State * L, CoronaMemoryKind kind, bool hashProduct = false )
{
    static int sWeakTableCookie;

    lua_pushlightuserdata( L, &sWeakTableCookie ); // ..., cookie
    lua_rawget( L, LUA_REGISTRYINDEX ); // ..., weak_tables?

    if (lua_isnil( L, -1 )) // no weak tables array yet?
    {
        lua_pop( L, 1 ); // ...
        lua_createtable( L, 4, 0 ); // ..., weak_tables
        lua_createtable( L, 0, 1 ); // ..., weak_tables, mt
        lua_pushliteral( L, "k" ); // ..., weak_tables, mt, "k"
        lua_setfield( L, -2, "__mode" ); // ..., weak_tables, mt = { __mode = "k" }

        for (int i = 1; i <= 3; ++i)
        {
            lua_pushvalue( L, -1 ); // ..., weak_tables, mt, mt[, mt[, mt]]
        }

        for (int i = 1; i <= 4; ++i) // readable, writable, readable hash products, writable hash products
        {
            lua_newtable( L ); // ..., weak_table, mt[, mt[, mt[, mt]]], wt
            lua_insert( L, -2 ); // ..., weak_tables[, mt[, mt[, mt]]], wt, mt
            lua_setmetatable( L, -2 ); // ..., weak_tables[, mt[, mt[, mt]]], wt; wt.metatable = mt
            lua_rawseti( L, -6 + i, i ); // ..., weak_tables = { ..., wt }[, mt[, mt[, mt]]]
        }

        lua_pushlightuserdata( L, &sWeakTableCookie ); // ..., weak_tables, cookie
        lua_pushvalue( L, -2 ); // ..., weak_tables, cookie, weak_tables
        lua_rawset( L, LUA_REGISTRYINDEX ); // ..., weak_tables; registry[cookie] = weak_tables
    }

    lua_rawgeti( L, -1, (kMemoryWrite == kind ? 2 : 1) + (hashProduct ? 2 : 0) ); // ..., weak_tables, wt
    lua_remove( L, -2 ); // ..., wt
}

static void
ObjectLookup( lua_State * L, int objectIndex, CoronaMemoryKind kind, bool hashProduct = false )
{
    GetWeakTable( L, kind, hashProduct ); // ..., object, ..., wt

    lua_pushvalue( L, objectIndex ); // ..., object, ..., wt, object
    lua_rawget( L, -2 ); // ..., object, ..., wt, info?
}

static bool
GetAssignedCallbacks( lua_State * L, int & objectIndex, CoronaMemoryKind kind, bool wantEnsureSizes )
{
	objectIndex = CoronaLuaNormalize( L, objectIndex );

	ObjectLookup( L, objectIndex, kind ); // ..., wt, callbacks_info?

	lua_remove( L, -2 ); // ..., callbacks_info?

	if (lua_istable( L, -1 )) // no info yet?
	{
		lua_getfield( L, -1, "callbacks" );// ..., callbacks_info?, callbacks?
		lua_remove( L, -2 ); // ..., callbacks?
	}

	if (lua_isnil( L, -1 ) || (wantEnsureSizes && !AsCallbacks( L, -1 )->ensureSizes))
	{
		lua_pop( L, 1 ); // ...

		return false;
	}

	else
	{
		return true;
	}
}

static void
GetCallbacksTable( lua_State * L )
{
	static int sCallbacksCookie;

	GetRegistryTable( L, &sCallbacksCookie ); // ..., callbacks
}

static void
GetLuaObjectReader( lua_State * L )
{
	static int sKeyCookie;

	GetCallbacksTable( L ); // ..., callbacks

	lua_pushlightuserdata( L, &sKeyCookie ); // ..., callbacks, cookie
	lua_rawget( L, LUA_REGISTRYINDEX ); // ..., callbacks, key?

	if (lua_isnil( L, -1 )) // no reader yet?
	{
		lua_pop( L, 1 ); // ..., callbacks

		CoronaMemoryCallbacks callbacks = {};

		callbacks.size = sizeof( CoronaMemoryCallbacks );

		callbacks.getBytes = []( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int * count, const CoronaMemoryExtra * )
		{
			*count = lua_objlen( L, objectIndex );

			void * result = NULL;

			switch (lua_type( L, objectIndex ))
			{
			case LUA_TSTRING:
				if (kMemoryRead == kind)
				{
					result = const_cast< char * >( lua_tostring( L, objectIndex ) );
				}

				else
				{
					lua_pushliteral( L, "Strings are read-only" ); // ..., message
				}

				break;
			case LUA_TUSERDATA:
				result = lua_touserdata( L, objectIndex );

				break;
			default:
				lua_pushliteral( L, "Expected string or full userdata" ); // ..., message
			}

			return result;
		};

		void * key = CoronaMemoryRegisterCallbacks( L, &callbacks, NULL, false, NULL );

		lua_pushlightuserdata( L, &sKeyCookie ); // ..., callbacks, cookie
		lua_pushlightuserdata( L, key ); // ..., callbacks, cookie, key
		lua_rawset( L, LUA_REGISTRYINDEX ); // ..., callbacks; registry[cookie] = key
		lua_pushlightuserdata( L, key ); // ..., callbacks, key
	}

	lua_rawget( L, -2 ); // ..., callbacks, LuaObjectReader
	lua_remove( L, -2 ); // ..., LuaObjectReader
}

static bool
HasImplicitReader( lua_State * L, int objectIndex, CoronaMemoryKind kind )
{
    return kMemoryRead == kind && lua_isstring( L, objectIndex );
}

CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireBytes( lua_State * L, int objectIndex, CoronaMemoryKind kind, void * params )
{
    objectIndex = CoronaLuaNormalize( L, objectIndex );

	bool found = GetAssignedCallbacks( L, objectIndex, kind, false ); // ..., object, ...[, reader / writer]

	if (!found && HasImplicitReader( L, objectIndex, kind ))
	{
		GetLuaObjectReader( L ); // ..., object, ..., LuaObjectReader

		found = true;
	}

	if (found)
	{
		return Acquire( L, objectIndex, kind, params );
	}

	else
	{
		ReplaceWithError( L, objectIndex, lua_gettop( L ), "Object has no %s or reader-writer", kMemoryRead == kind ? "reader" : "writer" );

		return NullHandle( );
	}
}

static CoronaMemoryHandle
EnsureSizeAndAcquire( lua_State * L, int objectIndex, CoronaMemoryKind kind, const unsigned int * expectedSizes, int sizeCount, void * params )
{
	CallbackInfo info( L ); // ...

	CoronaMemoryExtra extra = { params, info.fUserData };
	int oldTop = lua_gettop( L );

	if (!info.fCallbacks->ensureSizes)
	{
		ReplaceWithError( L, objectIndex, oldTop, "Missing 'ensureSizes()' callback" );
	}

	else if (BeforeGetBytes( L, objectIndex, oldTop, info, &extra ))
	{
		if (!info.fCallbacks->ensureSizes( L, objectIndex, expectedSizes, sizeCount, &extra )) // ...[, etc., error]
		{
			ReplaceWithError( L, objectIndex, oldTop, "'ensureSize()' callback failed" ); // ...
		}

		else
		{
			return GetBytesAndRest( L, objectIndex, oldTop, info, kind, &extra );
		}
	}

	return NullHandle( );
}

CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireBytes( lua_State * L, int objectIndex, CoronaMemoryKind kind, const unsigned int * expectedSizes, int sizeCount, void * params )
{
	objectIndex = CoronaLuaNormalize( L, objectIndex );

	if (GetAssignedCallbacks( L, objectIndex, kind, false )) // ..., object, ...[, reader / writer]
	{
		return EnsureSizeAndAcquire( L, objectIndex, kind, expectedSizes, sizeCount, params );
	}

	else
	{
		return ReplaceWithErrorAndReturnNullHandle( L, objectIndex, lua_gettop( L ), "Object has no %s or reader-writer", kMemoryRead == kind ? "reader" : "writer" );
	}
}

// ----------------------------------------------------------------------------

CORONA_API
const void * CoronaMemoryGetReadableBytes( lua_State * L, CoronaMemoryHandle * memoryHandle )
{
	return (CoronaMemoryGetPosition( L, memoryHandle ) && !memoryHandle->data->fWritable) ? memoryHandle->data->fBytes : NULL;
}

CORONA_API
void * CoronaMemoryGetWritableBytes( lua_State * L, CoronaMemoryHandle * memoryHandle )
{
	return (CoronaMemoryGetPosition( L, memoryHandle ) && memoryHandle->data->fWritable) ? const_cast< void * >( memoryHandle->data->fBytes ) : NULL;
}

CORONA_API
unsigned int CoronaMemoryGetByteCount( lua_State * L, CoronaMemoryHandle * memoryHandle )
{
	return CoronaMemoryGetPosition( L, memoryHandle ) ? memoryHandle->data->fByteCount : 0U;
}

CORONA_API
int CoronaMemoryGetStride( lua_State * L, CoronaMemoryHandle * memoryHandle, int strideIndex, unsigned int * stride )
{
	if (CoronaMemoryGetPosition( L, memoryHandle ))
	{
		if (strideIndex >= 1 && strideIndex <= memoryHandle->data->fStrideCount)
		{
			lua_getfenv( L, memoryHandle->lastKnownStackPosition ); // ..., env

			int has_strides = lua_objlen( L, -1 ) > 0U;

			if (has_strides)
			{
				lua_rawgeti( L, -1, strideIndex ); // ..., env, stride

				*stride = ( unsigned int )lua_tointeger( L, -1 );
			}

			else // n.b. sanity check (should never get here)
			{
				memoryHandle->data->fError = "No strides found";
			}

			lua_pop( L, 1 + has_strides ); // ...

			return has_strides;
		}

		else
		{
			memoryHandle->data->fError = "Out-of-bounds stride index";
		}
	}

	return 0;
}

CORONA_API
unsigned int CoronaMemoryGetStrideCount( lua_State * L, CoronaMemoryHandle * memoryHandle )
{
	return CoronaMemoryGetPosition( L, memoryHandle ) ? memoryHandle->data->fStrideCount : 0U;
}

// ----------------------------------------------------------------------------

CORONA_API
int CoronaMemoryRelease( lua_State * L, CoronaMemoryHandle * memoryHandle )
{
	if (CoronaMemoryGetPosition( L, memoryHandle ))
	{
		GetMemoryDataCache( L ); // ..., memoryData, ..., cache

		lua_pushvalue( L, memoryHandle->lastKnownStackPosition ); // ..., memoryData, ..., cache, memoryData
		lua_rawseti( L, -2, ( int )lua_objlen( L, -2 ) + 1 ); // ..., memoryData, ..., cache = { ..., memoryData }
		lua_getfenv( L, memoryHandle->lastKnownStackPosition ); // ..., memoryData, ..., cache, env

		for (int n = ( int )lua_objlen( L, -1 ); n > 0; --n) // wipe strides
		{
			lua_pushnil( L ); // ..., memoryData, ..., cache, env, nil
			lua_rawseti( L, -2, n ); // ..., memoryData, ..., cache, env = { ..., [#env] = nil, stash }
		}

        lua_pushnil( L ); // ..., memoryData, ..., cache, env, nil
        lua_setfield( L, -2, "stash" ); // ..., memoryData, ..., cache, env = { stash = nil }
		lua_pop( L, 2 ); // ..., memoryData, ...
		lua_pushboolean( L, 0 ); // ..., memoryData, ..., false
		lua_replace( L, memoryHandle->lastKnownStackPosition ); // ..., false, ...

		memoryHandle->lastKnownStackPosition = 0;

		return 1;
	}

	return 0;
}

// ----------------------------------------------------------------------------

static unsigned int
NewHash( )
{
    static Rtt_AbsoluteTime counter = Rtt_GetAbsoluteTime( );
    
    return std::hash< Rtt_AbsoluteTime >{}( counter++ );
}

static unsigned int
KeyHashProduct( void * key, unsigned int hash )
{
    return hash * std::hash< void * >{}( key );
}

static CoronaMemoryCallbacks *
NewCallbacks( lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, bool userDataFromStack, unsigned int * hash )
{
	lua_newuserdata( L, sizeof( CoronaMemoryCallbacks ) ); // ...[, userData], callbacks

	CoronaMemoryCallbacks * res = AsCallbacks( L, -1 );

	*res = *callbacks;

	if (name || userDataFromStack)
	{
		lua_createtable( L, 0, 4 ); // ...[, userData], callbacks, callbacks_info

		if (userDataFromStack)
		{
			res->userData = lua_touserdata( L, -3 );

			lua_pushvalue( L, -3 ); // ..., userData, callbacks, callbacks_info, userData
			lua_remove( L, -4 ); // ..., callbacks, callbacks_info, userData
			lua_setfield( L, -2, "userData" ); // ..., callbacks, callbacks_info = { name?, userData = userData }
		}

		if (name)
		{
			lua_pushstring( L, name ); // ..., callbacks, callbacks_info, name
			lua_setfield( L, -2, "name" ); // ..., callbacks, callbacks_info = { name = name, userData? }
            
            if (hash)
            {
                *hash = NewHash();
                
                lua_pushinteger( L, KeyHashProduct( res, *hash ) ); // ..., callbacks, callbacks_info, product
                lua_setfield( L, -2, "product" ); // ..., callbacks, callbacks_info = { name?, userData?, product = product }
            }
		}

		lua_insert( L, -2 ); // ..., callbacks_info, callbacks
		lua_setfield( L, -2, "callbacks" ); // ..., callbacks_info = { callbacks = callbacks, name?, userData?, product? }
	}

	return res;
}

static bool
CheckUserData( lua_State * L, bool userDataFromStack )
{
	return !userDataFromStack || lua_type( L, -1 ) == LUA_TUSERDATA;
}

CORONA_API
void * CoronaMemoryRegisterCallbacks( lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, int userDataFromStack, unsigned int * hash )
{
	CoronaMemoryCallbacks * res = NULL;

	if (sizeof( CoronaMemoryCallbacks ) == callbacks->size && callbacks->getBytes)
	{
		if (!CheckUserData( L, userDataFromStack ))
		{
			return NULL;
		}

		res = NewCallbacks( L, callbacks, name, userDataFromStack, hash ); // ..., callbacks_info

		GetCallbacksTable( L ); // ..., callbacks_info, callbacks_table

		lua_insert( L, -2 ); // ..., callbacks_table, callbacks_info
		lua_pushlightuserdata( L, res ); // ..., callbacks_table, callbacks_info, key
		lua_insert( L, -2 ); // ..., callbacks_table, key, callbacks_info
		lua_rawset( L, -3 ); // ..., callbacks_table = { ..., [key] = callbacks_info }
		lua_pop( L, 1 ); // ...
	}

	return res;
}

CORONA_API
void * CoronaMemoryFindCallbacks( lua_State * L, const char * name )
{
	void * key = NULL;

	if (name)
	{
		int top = lua_gettop( L );

		GetCallbacksTable( L ); // ..., callbacks

		for (lua_pushnil( L ); lua_next( L, -2 ); lua_pop( L, 1 ))
		{
			if (lua_istable( L, -1 ))
			{
				lua_getfield( L, -1, "name" ); // ..., callbacks, key, callbacks_info, name?

				if (lua_isstring( L, -1 ) && strcmp( name, lua_tostring( L, -1 ) ) == 0)
				{
					key = lua_touserdata( L, -3 );

					break;
				}

				lua_pop( L, 1 ); // ..., callbacks, key, callbacks_info
			}
		}

		lua_settop( L, top ); // ...
	}

	return key;
}

CORONA_API
int CoronaMemoryUnregisterCallbacks( lua_State * L, const char * name, unsigned int hash )
{
    GetCallbacksTable( L ); // ..., callbacks
    
    for (lua_pushnil( L ); lua_next( L, -2 ); lua_pop( L, 1 ))
    {
        if (lua_istable( L, -1 ))
        {
            lua_getfield( L, -1, "name" ); // ..., callbacks, key, callbacks_info, name?

            bool candidate = lua_isstring( L, -1 ) && strcmp( name, lua_tostring( L, -1 ) ) == 0;
            
            lua_pop( L, 1 ); // ..., callbacks, key, callbacks_info
            
            if (candidate)
            {
                lua_getfield( L, -1, "product" ); // ..., callbacks, key, callbacks_info, product?

                if (lua_isnumber( L, -1 ) && KeyHashProduct( lua_touserdata( L, -3 ), hash) == lua_tointeger( L, -1 ))
                {
                    lua_pop( L, 2 ); // ..., callbacks, key
                    lua_pushnil( L ); // ..., callbacks, key, nil
                    lua_rawset( L, -3 ); // ..., callbacks = { ..., [key] = nil }
                    lua_pop( L, 1 ); // ...
                    
                    return 1;
                }
                
                lua_pop( L, 1 ); // ..., callbacks, key, callbacks_info
            }
        }
    }

    lua_pop( L, 1 ); // ...
    
    return 0;
}

CORONA_API
void * CoronaMemoryGetCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind )
{
    if (GetAssignedCallbacks( L, objectIndex, kind, false )) // ...[, callbacks]
    {
        return lua_touserdata( L, -1 );
    }

    else if (HasImplicitReader( L, objectIndex, kind ))
    {
        GetLuaObjectReader( L ); // ..., LuaObjectReader

        return lua_touserdata( L, -1 );
    }
    
    return NULL;
}

static void
PushCallbacksFrom( lua_State * L, int index )
{
	if (lua_istable( L, index ))
	{
		lua_getfield( L, index, "callbacks" ); // ..., callbacks
	}

	else
	{
		lua_pushvalue( L, index ); // ..., callbacks
	}
}

static int
AssignInfoToObject( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int * hash )
{
	ObjectLookup( L, objectIndex, kind );	// ..., object, ..., callbacks_info, wt, info?

	if (lua_isnil( L, -1 )) // no info yet?
	{
		if (hash) // might want to remove?
		{
			GetWeakTable( L, kind, true ); // ..., object, ..., callbacks_info, wt, nil, hash_products_wt
			PushCallbacksFrom( L, -4 ); // ..., object, ..., callbacks_info, wt, nil, hash_products_wt, callbacks

			*hash = NewHash( );

			lua_pushvalue( L, objectIndex ); // ..., object, ..., callbacks_info, wt, nil, hash_products_wt, callbacks, object
			lua_pushinteger( L, KeyHashProduct( lua_touserdata( L, -2 ), *hash ) ); // ..., object, ..., callbacks_info, wt, nil, hash_products_wt, callbacks, object, product
			lua_rawset( L, -4 ); // ..., object, ..., callbacks_info, wt, nil, hash_products_wt = { ..., [object] = product }, callbacks
			lua_pop( L, 2 ); // ..., object, ..., callbacks_info, wt, nil
		}

		lua_pushvalue( L, objectIndex ); // ..., object, ..., callbacks_info, wt, nil, object
		lua_pushvalue( L, -4 ); // ..., object, ..., callbacks_info, wt, nil, object, callbacks_info
		lua_rawset( L, -4 ); // ..., object, ..., callbacks_info, wt = { ..., [object] = callbacks_info }, nil

		return 1;
	}

	return 0;
}

CORONA_API
void * CoronaMemorySetCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind, const CoronaMemoryCallbacks * callbacks, int userDataFromStack, unsigned int * hash )
{
    if (!lua_isnoneornil( L, objectIndex ) && sizeof( CoronaMemoryCallbacks ) == callbacks->size && callbacks->getBytes)
    {
        objectIndex = CoronaLuaNormalize( L, objectIndex );

        if (!CheckUserData( L, userDataFromStack ))
        {
            return NULL;
        }

        if (userDataFromStack && lua_gettop( L ) == objectIndex)
        {
            lua_pushvalue( L, objectIndex ); // ..., object, object
        }

        CoronaMemoryCallbacks * res = NewCallbacks( L, callbacks, NULL, userDataFromStack, NULL ); // ..., callbacks_info
        bool available = AssignInfoToObject( L, objectIndex, kind, hash ); // ..., callbacks_info, wt, info?

        lua_pop( L, 3 ); // ...

        return available ? res : NULL;
    }

    return NULL;
}

static bool
FoundCallbacksAt( lua_State * L, int index, void * object )
{
	if (lua_istable( L, index ))
	{
		lua_getfield( L, index, "callbacks" ); // ..., callbacks_info?, ..., callbacks?

		index = -1; // n.b. FindObjectsOnStack uses positive indices
	}

	bool found = lua_type( L, index ) == LUA_TUSERDATA && lua_topointer( L, index ) == object;

	if (-1 == index)
	{
		lua_pop( L, 1 ); // ..., callbacks_info, ...
	}

	return found;
}

CORONA_API
int CoronaMemorySetCallbacksByKey( lua_State * L, int objectIndex, CoronaMemoryKind kind, void * key, unsigned int * hash )
{
    int available = 0;

    if (key && !lua_isnoneornil( L, objectIndex ))
    {
        objectIndex = CoronaLuaNormalize( L, objectIndex );

        int top = lua_gettop( L );

        GetCallbacksTable( L ); // ..., object, ..., callbacks

        lua_pushlightuserdata( L, key ); // ..., object, ..., callbacks, key
        lua_rawget( L, -2 ); // ..., object, ..., callbacks, callbacks_info?

        if (lua_isnil( L, -1 ))
        {
            int callbackIndex = FindObjectOnStack( L, key, FoundCallbacksAt );

            if (callbackIndex)
            {
                lua_pop( L, 1 ); // ..., object, ..., callbacks
                lua_pushvalue( L, callbackIndex ); // ..., object, ..., callbacks, callbacks_info
            }
        }

        if (!lua_isnil( L, -1 ))
        {
            available = AssignInfoToObject( L, objectIndex, kind, hash ); // ..., object, ..., callbacks, callbacks_info, wt, info?
        }

        lua_settop( L, top ); // ..., object, ...
    }

    return available;
}

CORONA_API
int CoronaMemoryRemoveCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int hash )
{
    objectIndex = CoronaLuaNormalize( L, objectIndex );
    
    int result = 0, top = lua_gettop( L );

    ObjectLookup( L, objectIndex, kind, true ); // ..., object, ..., hash_products, product?

    if (!lua_isnil( L, -1 ))
    {
        ObjectLookup( L, objectIndex, kind ); // ..., object, ..., hash_products, product, wt, callbacks

        if (KeyHashProduct( lua_touserdata( L, -1 ), hash ) == ( size_t )luaL_optnumber( L, -3, 0U ))
        {
            lua_pushvalue( L, objectIndex ); // ..., object, ..., hash_products, product, wt, callbacks, object
            lua_pushnil( L ); // ..., object, ..., hash_products, product, wt, callbacks, object, nil
            lua_rawset( L, -4 ); // ..., object, ..., hash_products, product, wt = { ..., [object] = nil }, callbacks

            result = 1;
        }
    }

    lua_settop( L, top ); // ..., object, ...

    return result;
}

// ----------------------------------------------------------------------------

CORONA_API
int CoronaMemoryGetAlignment( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int * alignment )
{
    if (GetAssignedCallbacks( L, objectIndex, kind, false )) // ...[, callbacks]
    {
        *alignment = AsCallbacks( L, -1 )->alignment;

        lua_pop( L, 1 ); // ...

        if (*alignment > 0U && (*alignment & (*alignment - 1U)) != 0U) // not a power of 2?
        {
            return 0;
        }

        if (*alignment < alignof( void * )) // 0 or too-small power?
        {
            *alignment = alignof( void * );
        }

        return 1;
    }

    return 0;
}

static int
FindResult( lua_State * L, int objectIndex, CoronaMemoryKind kind, bool wantEnsureSize )
{
	if (GetAssignedCallbacks( L, objectIndex, kind, wantEnsureSize )) // ...[, callbacks]
	{
		lua_pop( L, 1 ); // ...

		return 1;
	}

	else
	{
		return 0;
	}
}

CORONA_API
int CoronaMemoryIsAvailable( lua_State * L, int objectIndex, CoronaMemoryKind kind )
{
	return HasImplicitReader( L, objectIndex, kind ) || FindResult( L, objectIndex, kind, false );
}

CORONA_API
int CoronaMemoryIsResizable( lua_State * L, int objectIndex, CoronaMemoryKind kind )
{
	return FindResult( L, objectIndex, kind, true );
}

// ----------------------------------------------------------------------------
