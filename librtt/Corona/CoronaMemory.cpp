//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Array.h"
#include "Core/Rtt_Time.h"

#include "CoronaMemory.h"
#include "CoronaLua.h"

#include <functional>

struct CoronaMemoryData {
	CoronaMemoryData();
	CoronaMemoryData(const CoronaMemoryData & other) = default;

	const void * fBytes;
	unsigned int fByteCount;
	unsigned int fStrideCount;
	bool fWritable;
};

CoronaMemoryData::CoronaMemoryData () :
	fBytes(nullptr),
	fByteCount(0U),
	fStrideCount(0U),
	fWritable(false)
{
}


CORONA_API
int CoronaMemoryIsValid(lua_State * L, CoronaMemoryHandle * memoryHandle)
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

static int FindObjectOnStack (lua_State * L, void * object, bool (*find)(lua_State * L, int index, void * object))
{
	for (int i = 1, top = lua_gettop(L); i <= top; ++i)
	{
		if (find(L, i, object))
		{
			return i;
		}
	}

	return 0;
}

static bool FoundObjectAt (lua_State * L, int index, void * object)
{
	return lua_type(L, index) == LUA_TUSERDATA && lua_topointer(L, index) == object;
}

CORONA_API
int CoronaMemoryGetPosition (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	if (!CoronaMemoryIsValid(L, memoryHandle))
	{
		return 0;
	}

	else if (!FoundObjectAt(L, memoryHandle->lastKnownStackPosition, memoryHandle->data))
	{
		memoryHandle->lastKnownStackPosition = FindObjectOnStack(L, memoryHandle->data, FoundObjectAt);
	}

	return memoryHandle->lastKnownStackPosition;
}

static void GetWeakTable (lua_State * L, bool writable, bool hashes = false)
{
	static int nonce;

	lua_pushlightuserdata(L, &nonce); // ..., nonce
	lua_rawget(L, LUA_REGISTRYINDEX); // ..., weak_tables?

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1); // ...
		lua_createtable(L, 2, 0); // ..., weak_tables
		lua_createtable(L, 0, 1); // ..., weak_tables, mt
		lua_pushliteral(L, "k"); // ..., weak_tables, mt, "k"
		lua_setfield(L, -2, "__mode"); // ..., weak_tables, mt = { __mode = "k" }

		for (int i = 1; i <= 3; ++i)
		{
			lua_pushvalue(L, -1); // ..., weak_tables, mt, mt[, mt[, mt]]
		}

		for (int i = 1; i <= 4; ++i) // {readable, writable, readable hashes, writable hashes}
		{
			lua_newtable(L); // ..., weak_table, mt[, mt[, mt[, mt]]], wt
			lua_insert(L, -2); // ..., weak_tables[, mt[, mt[, mt]]], wt, mt
			lua_setmetatable(L, -2); // ..., weak_tables[, mt[, mt[, mt]]]], wt; wt.metatable = mt
			lua_rawseti(L, -6 + i, i); // ..., weak_tables = { ..., wt }[, mt]
		}

		lua_pushlightuserdata(L, &nonce); // ..., weak_tables, nonce
		lua_pushvalue(L, -2); // ..., weak_tables, nonce, weak_tables
		lua_rawset(L, LUA_REGISTRYINDEX); // ..., weak_tables; registry[nonce] = weak_tables
	}

	lua_rawgeti(L, -1, (writable ? 2 : 1) + (hashes ? 2 : 0)); // ..., weak_tables, wt
	lua_remove(L, -2); // ..., wt
}

static bool GetAssignedCallbacks (lua_State * L, int & objectIndex, bool writable, bool wantEnsureSizes)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	GetWeakTable(L, writable); // ..., wt

	lua_pushvalue(L, objectIndex); // ..., wt, object
	lua_rawget(L, -2); // ..., wt, callbacks_info?
	lua_remove(L, -2); // ..., callbacks_info?

	if (lua_istable(L, -1))
	{
		lua_getfield(L, -1, "callbacks");// ..., callbacks_info?, callbacks?
		lua_remove(L, -2); // ..., callbacks?
	}

	if (lua_isnil(L, -1) || (wantEnsureSizes && !((CoronaMemoryCallbacks *)lua_touserdata(L, -1))->ensureSizes))
	{
		lua_pop(L, 1); // ...

		return false;
	}

	else
	{
		return true;
	}
}

struct CallbackInfo {
	CallbackInfo (lua_State * L) :
		fName(nullptr),
		fUserData(nullptr)
	{
		if (lua_istable(L, -1))
		{
			lua_getfield(L, -1, "callbacks"); // ..., t, callbacks
			lua_getfield(L, -2, "name"); // ..., t, callbacks, name?
			lua_getfield(L, -3, "userData"); // ..., t, callbacks, name?, userData?

			fCallbacks = (CoronaMemoryCallbacks *)lua_touserdata(L, -3);
			fUserData = lua_touserdata(L, -1);

			if (lua_isstring(L, -2)) fName = lua_tostring(L, -2);

			lua_pop(L, 1); // ..., t
		}

		else
		{
			fCallbacks = (CoronaMemoryCallbacks *)lua_touserdata(L, -1);
		}

		luaL_argcheck(L, -1, fCallbacks->size == sizeof(CoronaMemoryCallbacks), "Incompatible memory callbacks");
		lua_pop(L, 1);	// ...
	}

	CoronaMemoryCallbacks * fCallbacks;
	const char * fName;
	void * fUserData;
};

static void GetRegistryTable (lua_State * L, void * nonce)
{
	lua_pushlightuserdata(L, nonce); // ..., nonce
	lua_rawget(L, LUA_REGISTRYINDEX); // ..., t?

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1); // ...
		lua_newtable(L); // ..., t
		lua_pushlightuserdata(L, nonce); // ..., t, nonce
		lua_pushvalue(L, -2); // ..., t, nonce, t
		lua_rawset(L, LUA_REGISTRYINDEX); // ..., t; registry[nonce] = t
	}
}

static void GetMemoryDataCache (lua_State * L)
{
	static int nonce;

	GetRegistryTable(L, &nonce); // ..., cache
}

static CoronaMemoryData * GetMemoryData (lua_State * L, const CoronaMemoryData & work)
{
	GetMemoryDataCache(L); // ..., cache

	lua_rawgeti(L, -1, (int)lua_objlen(L, -1)); // ..., cache, memoryData?
	lua_remove(L, -2); // ..., memoryData?

	CoronaMemoryData * memoryData;

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1); // ...

		memoryData = (CoronaMemoryData *)lua_newuserdata(L, sizeof(CoronaMemoryData)); // ..., memoryData

		lua_createtable(L, 1, 0); // ..., memoryData, env
		lua_setfenv(L, -2); // ..., memoryData; data.environment = env
	}

	else
	{
		memoryData = (CoronaMemoryData *)lua_touserdata(L, -1);
	}

	new (memoryData) CoronaMemoryData(work);

	return memoryData;
}

static void ReplaceWithError (lua_State * L, int objectIndex, int oldTop, const char * defaultError)
{
	int newTop = lua_gettop(L);

	if (newTop == oldTop) lua_pushstring(L, defaultError); // ..., object, ..., etc., defError

	else if (lua_isstring(L, -1))
	{
		lua_pushstring(L, defaultError); // ..., object, ..., etc., error, defError
		lua_insert(L, -2); // ..., object, ..., etc., defError, error
		lua_pushliteral(L, ": "); // ..., object, ..., etc., defError, error, ": "
		lua_insert(L, -2); // ..., object, ..., etc., defError, ": ", error
		lua_concat(L, 3); // ..., object, ..., etc., defError .. ": " .. error
	}

	lua_replace(L, objectIndex); // ..., error, ..., etc.
	lua_settop(L, oldTop); // ..., error, ...
}

static bool BeforeGetBytes (lua_State * L, int objectIndex, int oldTop, const CallbackInfo & info, CoronaMemoryExtra * extra)
{
	if (info.fCallbacks->canAcquire && !info.fCallbacks->canAcquire(L, objectIndex, extra)) // ...[, etc., error]
	{
		ReplaceWithError(L, objectIndex, oldTop, "'canAcquire()' callback failed"); // ...

		return false;
	}

	lua_settop(L, oldTop); // ...

	return true;
}

static CoronaMemoryHandle NullHandle (void)
{
	return {0, nullptr};
}

static CoronaMemoryHandle GetBytesAndRest (lua_State * L, int objectIndex, int oldTop, const CallbackInfo & info, bool writable, CoronaMemoryExtra * extra)
{
	CoronaMemoryData work;

	work.fBytes = info.fCallbacks->getBytes(L, objectIndex, &work.fByteCount, writable, extra); // ...[, etc., error]

	if (!work.fBytes)
	{
		ReplaceWithError(L, objectIndex, oldTop, "'getBytes()' callback failed");

		return NullHandle();
	}

	lua_settop(L, oldTop); // ...

	if (info.fCallbacks->getStrides && !info.fCallbacks->getStrides(L, objectIndex, &work.fStrideCount, extra)) // ...[, etc. / strides, error]
	{
		ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback failed"); // ...

		return NullHandle();
	}

	if (work.fStrideCount > 0U)
	{
		if (oldTop + work.fStrideCount > lua_gettop(L))
		{
			ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback reports more strides than found on stack"); // ...

			return NullHandle();
		}

		unsigned strideProduct = 1U;

		for (int i = 1; i <= work.fStrideCount; ++i)
		{
			if (!lua_isnumber(L, -i))
			{
				ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback returned non-numeric stride"); // ...

				return NullHandle();
			}

			strideProduct *= (unsigned int)lua_tointeger(L, -i);
		}

		if (strideProduct > work.fByteCount)
		{
			ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback returns stride spanning more bytes than received"); // ...

			return NullHandle();
		}
	}

	lua_pushvalue(L, objectIndex); // ..., object, ...[, etc., strides], object

	CoronaMemoryData * memoryData = GetMemoryData(L, work); // ..., object, ...[, etc., strides], object, memoryData

	lua_replace(L, objectIndex); // ..., memoryData, ...[, etc., strides], object

	memoryData->fWritable = writable;

	CoronaMemoryHandle handle = { objectIndex, memoryData };

	CoronaMemoryAddToStash(L, &handle); // ..., memoryData, ...[, etc., strides]

	if (work.fStrideCount > 0U)
	{
		lua_getfenv(L, objectIndex); // ..., memoryData, ...[, etc.], strides, env
		lua_getfield(L, -1, "strides"); // ..., memoryData, ...[, etc.], strides, env, envStrides

		if (lua_isnil(L, -1))
		{
			lua_pop(L, 1); // ..., memoryData, ...[, etc.], strides, env
			lua_newtable(L); // ..., memoryData, ...[, etc.], strides, env, envStrides
			lua_pushvalue(L, -1); // ..., memoryData, ...[, etc.], strides, env, envStrides, envStrides
			lua_setfield(L, -3, "strides"); // ..., memoryData, ...[, etc.], strides, env = { ..., strides = envStrides }, envStrides
		}

		int before_strides = -(2 + work.fStrideCount);

		for (int i = 1; i <= work.fStrideCount; ++i)
		{
			lua_pushvalue(L, before_strides + i); // ..., memoryData, ...[, etc.], strides, env, envStrides, stride
			lua_rawseti(L, -2, i); // ..., memoryData, ...[, etc.], strides, env, envStrides = { ..., stride }
		}
	}

	lua_settop(L, oldTop); // ..., memoryData, ...

	return handle;
}

static CoronaMemoryHandle Acquire (lua_State * L, int objectIndex, bool writable, void * params)
{
	CallbackInfo info(L); // ...

	CoronaMemoryExtra extra = {params, info.fUserData};
	int oldTop = lua_gettop(L);

	if (BeforeGetBytes(L, objectIndex, oldTop, info, &extra))
	{
		return GetBytesAndRest(L, objectIndex, oldTop, info, writable, &extra);
	}

	else
	{
		return NullHandle();
	}
}

static void GetCallbacksTable (lua_State * L)
{
	static int nonce;

	GetRegistryTable(L, &nonce); // ..., callbacks
}

static void GetLuaObjectReader (lua_State * L)
{
	static int nonce;

	GetCallbacksTable(L); // ..., callbacks

	lua_pushlightuserdata(L, &nonce); // ..., callbacks, nonce
	lua_rawget(L, LUA_REGISTRYINDEX); // ..., callbacks, key?

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1); // ..., callbacks

		CoronaMemoryCallbacks callbacks = { 0 };

		callbacks.size = sizeof(CoronaMemoryCallbacks);

		callbacks.getBytes = [](lua_State * L, int objectIndex, unsigned int * count, int writable, CoronaMemoryExtra *)
		{
			*count = lua_objlen(L, objectIndex);

			void * result = nullptr;

			switch (lua_type(L, objectIndex))
			{
			case LUA_TSTRING:
				if (!writable)
				{
					result = const_cast<char *>(lua_tostring(L, objectIndex));
				}

				else
				{
					lua_pushliteral(L, "Strings are read-only"); // ..., message
				}

				break;
			case LUA_TUSERDATA:
				result = lua_touserdata(L, objectIndex);

				break;
			default:
				lua_pushlightuserdata(L, "Expected string or full userdata"); // ..., message
			}

			return result;
		};

		void * key = CoronaMemoryRegisterCallbacks(L, &callbacks, nullptr, false);

		lua_pushlightuserdata(L, &nonce); // ..., callbacks, nonce
		lua_pushlightuserdata(L, key); // ..., callbacks, nonce, key
		lua_rawset(L, LUA_REGISTRYINDEX); // ..., callbacks; registry[nonce] = key
		lua_pushlightuserdata(L, key); // ..., callbacks, key
	}

	lua_rawget(L, -2); // ..., callbacks, LuaObjectReader
	lua_remove(L, -2); // ..., LuaObjectReader
}

CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireReadableBytes (lua_State * L, int objectIndex, void * params)
{
	bool found = GetAssignedCallbacks(L, objectIndex, 0, false); // ..., object, ...[, reader]

	if (!found && lua_isstring(L, objectIndex))
	{
		GetLuaObjectReader(L); // ..., object, ..., LuaObjectReader

		found = true;
	}

	if (found)
	{
		return Acquire(L, objectIndex, 0, params);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader or reader-writer");

		return NullHandle();
	}
}

CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireWritableBytes (lua_State * L, int objectIndex, void * params)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (GetAssignedCallbacks(L, objectIndex, 1, false)) // ..., object, ...[, writer]
	{
		return Acquire(L, objectIndex, 1, params);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader-writer");

		return NullHandle();
	}
}

static CoronaMemoryHandle EnsureSizeAndAcquire (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, bool writable, void * params)
{
	CallbackInfo info(L); // ...

	CoronaMemoryExtra extra = {params, info.fUserData};
	int oldTop = lua_gettop(L);

	if (!info.fCallbacks->ensureSizes)
	{
		ReplaceWithError(L, objectIndex, oldTop, "Missing 'ensureSizes()' callback");
	}

	else if (BeforeGetBytes(L, objectIndex, oldTop, info, &extra))
	{
		if (!info.fCallbacks->ensureSizes(L, objectIndex, expectedSizes, sizeCount, &extra)) // ...[, etc., error]
		{
			ReplaceWithError(L, objectIndex, oldTop, "'ensureSize()' callback failed"); // ...
		}

		else
		{
			return GetBytesAndRest(L, objectIndex, oldTop, info, writable, &extra);
		}
	}

	return NullHandle();
}

CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireReadableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * params)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (GetAssignedCallbacks(L, objectIndex, 0, false)) // ..., object, ...[, reader]
	{
		return EnsureSizeAndAcquire(L, objectIndex, expectedSizes, sizeCount, 0, params);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader or reader-writer");

		return NullHandle();
	}
}

CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireWritableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * params)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (GetAssignedCallbacks(L, objectIndex, 1, false)) // ..., object, ...[, writer]
	{
		return EnsureSizeAndAcquire(L, objectIndex, expectedSizes, sizeCount, 1, params);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no writer or reader-writer");

		return NullHandle();
	}
}

CORONA_API
const void * CoronaMemoryGetReadableBytes (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	return (CoronaMemoryGetPosition(L, memoryHandle) && !memoryHandle->data->fWritable) ? memoryHandle->data->fBytes : nullptr;
}

CORONA_API
void * CoronaMemoryGetWritableBytes (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	return (CoronaMemoryGetPosition(L, memoryHandle) && memoryHandle->data->fWritable) ? const_cast<void *>(memoryHandle->data->fBytes) : nullptr;
}

CORONA_API
unsigned int CoronaMemoryGetByteCount (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	return CoronaMemoryGetPosition(L, memoryHandle) ? memoryHandle->data->fByteCount : 0U;
}

CORONA_API
int CoronaMemoryGetStride (lua_State * L, CoronaMemoryHandle * memoryHandle, int strideIndex, unsigned int * stride)
{
	if (CoronaMemoryGetPosition(L, memoryHandle) && strideIndex >= 1 && strideIndex <= memoryHandle->data->fStrideCount)
	{
		lua_getfenv(L, memoryHandle->lastKnownStackPosition); // ..., env
		lua_getfield(L, -1, "strides"); // ..., env, strides

		int has_strides = lua_istable(L, -1);

		if (has_strides)
		{
			lua_rawgeti(L, -1, strideIndex); // ..., env, strides, stride

			*stride = (unsigned int)lua_tointeger(L, -1);
		}

		lua_pop(L, 2 + has_strides); // ...

		return has_strides;
	}

	return 0;
}

CORONA_API
unsigned int CoronaMemoryGetStrideCount (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	return CoronaMemoryGetPosition(L, memoryHandle) ? memoryHandle->data->fStrideCount : 0U;
}

static int StackSize (lua_State * L, int index)
{
	return (int)lua_objlen(L, index);
}

CORONA_API
int CoronaMemoryRelease (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	if (CoronaMemoryGetPosition(L, memoryHandle))
	{
		GetMemoryDataCache(L); // ..., memoryData, ..., cache

		lua_pushvalue(L, memoryHandle->lastKnownStackPosition); // ..., memoryData, ..., cache, memoryData
		lua_rawseti(L, -2, StackSize(L, -1) + 1); // ..., memoryData, ..., cache = { ..., memoryData }
		lua_getfenv(L, memoryHandle->lastKnownStackPosition); // ..., memoryData, ..., cache, env

		for (int n = (int)lua_objlen(L, -1); n > 0; --n)
		{
			lua_pushnil(L); // ..., memoryData, ..., cache, env, nil
			lua_rawseti(L, -2, n); // ..., memoryData, ..., cache, env = { ..., [#env] = nil }
		}

		lua_pop(L, 2); // ..., memoryData, ...
		lua_pushboolean(L, 0); // ..., memoryData, ..., false
		lua_replace(L, memoryHandle->lastKnownStackPosition); // ..., false, ...

		memoryHandle->lastKnownStackPosition = 0;

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaMemoryAddToStash (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	if (CoronaMemoryGetPosition(L, memoryHandle) && memoryHandle->lastKnownStackPosition != lua_gettop(L))
	{
		lua_getfenv(L, memoryHandle->lastKnownStackPosition); // ..., object, env
		lua_insert(L, -2); // ..., env, object
		lua_rawseti(L, -2, StackSize(L, -2) + 1); // ..., env = { ..., object }
		lua_pop(L, 1); // ...

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaMemoryRemoveFromStash (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	if (CoronaMemoryGetPosition(L, memoryHandle))
	{
		lua_getfenv(L, memoryHandle->lastKnownStackPosition); // ..., env

		int size = StackSize(L, -1);

		if (size > 0)
		{
			lua_pushnil(L); // ..., env, nil
			lua_rawseti(L, -2, size); // ..., env = { ..., [#env] = nil }
			lua_pop(L, 1); // ...

			return 1;
		}

		else
		{
			lua_pop(L, 1); // ...
		}
	}

	return 0;
}

CORONA_API
int CoronaMemoryPeekAtStash (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	if (CoronaMemoryGetPosition(L, memoryHandle))
	{
		lua_getfenv(L, memoryHandle->lastKnownStackPosition); // ..., env

		int size = StackSize(L, -1);

		if (size > 0)
		{
			lua_rawgeti(L, -1, size); // ..., env, object
			lua_remove(L, -2); // ..., object

			return 1;
		}

		else
		{
			lua_pop(L, 1); // ...
		}
	}

	return 0;
}

static CoronaMemoryCallbacks * NewCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, bool userDataFromStack)
{
	CoronaMemoryCallbacks * res = (CoronaMemoryCallbacks *)lua_newuserdata(L, sizeof(CoronaMemoryCallbacks)); // ...[, userData], callbacks

	*res = *callbacks;

	if (name || userDataFromStack)
	{
		lua_createtable(L, 0, 3); // ...[, userData], callbacks, callbacks_info

		if (userDataFromStack)
		{
			res->userData = lua_touserdata(L, -4);

			lua_pushvalue(L, -3); // ..., userData, callbacks, callbacks_info, userData
			lua_remove(L, -4); // ..., callbacks, callbacks_info, userData
			lua_setfield(L, -2, "userData"); // ..., callbacks, callbacks_info = { name?, userData = userData }
		}

		if (name)
		{
			lua_pushstring(L, name); // ..., callbacks, callbacks_info, name
			lua_setfield(L, -2, "name"); // ..., callbacks, callbacks_info = { name = name, userData? }
		}

		lua_insert(L, -2); // ..., callbacks_info, callbacks
		lua_setfield(L, -2, "callbacks"); // ..., callbacks_info = { callbacks = callbacks, name?, userData? }
	}

	return res;
}

static bool CheckUserData (lua_State * L, bool userDataFromStack)
{
	return !userDataFromStack || lua_type(L, -1) == LUA_TUSERDATA;
}

CORONA_API
void * CoronaMemoryRegisterCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, int userDataFromStack)
{
	CoronaMemoryCallbacks * res = nullptr;

	if (sizeof(CoronaMemoryCallbacks) == callbacks->size && callbacks->getBytes)
	{
		if (!CheckUserData(L, userDataFromStack))
		{
			return nullptr;
		}

		res = NewCallbacks(L, callbacks, name, userDataFromStack); // ..., callbacks_info

		GetCallbacksTable(L); // ..., callbacks_info, callbacks_table

		lua_insert(L, -2); // ..., callbacks_table, callbacks_info
		lua_pushlightuserdata(L, res); // ..., callbacks_table, callbacks_info, key
		lua_insert(L, -2); // ..., callbacks_table, key, callbacks_info
		lua_rawset(L, -3); // ..., callbacks_table = { ..., [key] = callbacks_info }
		lua_pop(L, 1); // ...
	}

	return res;
}

CORONA_API
void * CoronaMemoryFindCallbacks (lua_State * L, const char * name)
{
	void * key = nullptr;

	if (name)
	{
		int top = lua_gettop(L);

		GetCallbacksTable(L); // ..., callbacks

		for (lua_pushnil(L); lua_next(L, -1); lua_pop(L, 1))
		{
			if (lua_istable(L, -1))
			{
				lua_getfield(L, -1, "name"); // ..., callbacks, key, callbacks_info, name?

				if (lua_isstring(L, -1) && strcmp(name, lua_tostring(L, -1)) == 0)
				{
					key = lua_touserdata(L, -3);

					break;
				}

				lua_pop(L, 1); // ..., callbacks, key, callbacks_info
			}
		}

		lua_settop(L, top); // ...
	}

	return key;
}

static void ObjectInfoLookup (lua_State * L, int objectIndex, bool writable)
{
	GetWeakTable(L, writable); // ..., object, ..., wt

	lua_pushvalue(L, objectIndex); // ..., object, ..., wt, object
	lua_rawget(L, -2); // ..., object, ..., wt, info?
}

static int AssignInfoToObject (lua_State * L, int objectIndex, bool writable, unsigned int * hash)
{
	ObjectInfoLookup(L, objectIndex, writable);	// ..., object, ..., callbacks_info, wt, info?

	if (lua_isnil(L, -1))
	{
		if (hash)
		{
			GetWeakTable(L, writable, true); // ..., object, ..., callbacks_info, wt, nil, hashes_wt

			if (lua_istable(L, -4))
			{
				lua_getfield(L, -4, "callbacks"); // ..., object, ..., callbacks_info, wt, nil, hashes_wt, callbacks
			}

			else
			{
				lua_pushvalue(L, 4); // ..., object, ..., callbacks_info, wt, nil, hashes_wt, callbacks
			}

			void * key = lua_touserdata(L, -1);
			*hash = std::hash<uint64_t>{}(Rtt_GetAbsoluteTime());

			lua_pushvalue(L, objectIndex); // ..., object, ..., callbacks_info, wt, nil, hashes_wt, callbacks, object
			lua_pushinteger(L, *hash * std::hash<void *>{}(key)); // ..., object, ..., callbacks_info, wt, nil, hashes_wt, callbacks, object, product
			lua_rawset(L, -4); // ..., object, ..., callbacks_info, wt, nil, hashes_wt = { ..., [object] = product }, callbacks
			lua_pop(L, 2); // ..., object, ..., callbacks_info, wt, nil
		}

		lua_pushvalue(L, objectIndex); // ..., object, ..., callbacks, callbacks_info, wt, nil, object
		lua_pushvalue(L, -4); // ..., object, ..., callbacks, callbacks_info, wt, nil, object, callbacks_info
		lua_rawset(L, -4); // ..., object, ..., callbacks, callbacks_info, wt = { ..., [object] = callbacks_info }, nil

		return 1;
	}

	return 0;
}

static void * SetCallbacks (lua_State * L, int objectIndex, const CoronaMemoryCallbacks * callbacks, bool writable, bool userDataFromStack, unsigned int * hash)
{
	if (!lua_isnoneornil(L, objectIndex) && sizeof(CoronaMemoryCallbacks) == callbacks->size && callbacks->getBytes)
	{
		objectIndex = CoronaLuaNormalize(L, objectIndex);

		if (!CheckUserData(L, userDataFromStack))
		{
			return 0;
		}

		if (userDataFromStack && lua_gettop(L) == objectIndex)
		{
			lua_pushvalue(L, objectIndex); // ..., object, object
		}

		CoronaMemoryCallbacks * res = NewCallbacks(L, callbacks, nullptr, userDataFromStack); // ..., callbacks_info
		bool available = AssignInfoToObject(L, objectIndex, writable, hash); // ..., callbacks_info, wt, info?

		lua_pop(L, 3); // ...

		return available ? res : nullptr;
	}

	return 0;
}

CORONA_API
void * CoronaMemorySetReadCallbacks (lua_State * L, int objectIndex, const CoronaMemoryCallbacks * callbacks, int userDataFromStack, unsigned int * hash)
{
	return SetCallbacks(L, objectIndex, callbacks, false, userDataFromStack, hash);
}

CORONA_API
void * CoronaMemorySetWriteCallbacks (lua_State * L, int objectIndex, const CoronaMemoryCallbacks * callbacks, int userDataFromStack, unsigned int * hash)
{
	return SetCallbacks(L, objectIndex, callbacks, true, userDataFromStack, hash);
}

static bool FoundCallbacksAt (lua_State * L, int index, void * object)
{
	if (lua_istable(L, index))
	{
		lua_getfield(L, index, "callbacks"); // ..., callbacks_info?, ..., callbacks?

		index = -1; // n.b. FindObjectsOnStack uses positive indices
	}

	bool found = lua_type(L, index) == LUA_TUSERDATA && lua_topointer(L, index) == object;

	if (-1 == index)
	{
		lua_pop(L, 1); // ..., callbacks_info, ...
	}

	return found;
}

static int SetCallbacksByKey (lua_State * L, int objectIndex, void * key, bool writable, unsigned int * hash)
{
	int available = 0;

	if (key && !lua_isnoneornil(L, objectIndex))
	{
		objectIndex = CoronaLuaNormalize(L, objectIndex);

		int top = lua_gettop(L);

		GetCallbacksTable(L); // ..., object, ..., callbacks

		lua_pushlightuserdata(L, key); // ..., object, ..., callbacks, key
		lua_rawget(L, -2); // ..., object, ..., callbacks, callbacks_info?

		if (lua_isnil(L, -1))
		{
			int callbackIndex = FindObjectOnStack(L, key, FoundCallbacksAt);

			if (callbackIndex)
			{
				lua_pop(L, 1); // ..., object, ..., callbacks
				lua_pushvalue(L, callbackIndex); // ..., object, ..., callbacks, callbacks_info
			}
		}

		if (!lua_isnil(L, -1))
		{
			available = AssignInfoToObject(L, objectIndex, writable, hash); // ..., object, ..., callbacks, callbacks_info, wt, info?
		}

		lua_settop(L, top); // ..., object, ...
	}

	return available;
}

CORONA_API
int CoronaMemorySetReadCallbacksByKey (lua_State * L, int objectIndex, void * key, unsigned int * hash)
{
	return SetCallbacksByKey(L, objectIndex, key, false, hash);
}

CORONA_API
int CoronaMemorySetWriteCallbacksByKey (lua_State * L, int objectIndex, void * key, unsigned int * hash)
{
	return SetCallbacksByKey(L, objectIndex, key, true, hash);
}

CORONA_API
int CoronaMemoryRemoveReadCallbacks (lua_State * L, int objectIndex, unsigned int hash)
{
	return 0;
}

CORONA_API
int CoronaMemoryRemoveWriteCallbacks (lua_State * L, int objectIndex, unsigned int hash)
{
	return 0;
}

static int GetAlignment (lua_State * L, int objectIndex, bool writable, unsigned int * alignment)
{
	if (GetAssignedCallbacks(L, objectIndex, writable, false)) // ...[, callbacks]
	{
		*alignment = ((CoronaMemoryCallbacks *)lua_touserdata(L, -1))->alignment;

		lua_pop(L, 1); // ...

		if (*alignment > 0U && (*alignment & (*alignment - 1U)) != 0U) // not a power of 2?
		{
			return 0;
		}

		if (*alignment < alignof(void *)) // 0 or too-small power of 2?
		{
			*alignment = alignof(void *);
		}

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaMemoryGetReadAlignment (lua_State * L, int objectIndex, unsigned int * alignment)
{
	return GetAlignment(L, objectIndex, false, alignment);
}

CORONA_API
int CoronaMemoryGetWriteAlignment (lua_State * L, int objectIndex, unsigned int * alignment)
{
	return GetAlignment(L, objectIndex, true, alignment);
}

static int FindResult (lua_State * L, int objectIndex, bool writable, bool wantEnsureSize)
{
	if (GetAssignedCallbacks(L, objectIndex, writable, wantEnsureSize)) // ...[, callbacks]
	{
		lua_pop(L, 1); // ...

		return 1;
	}

	else
	{
		return 0;
	}
}

CORONA_API
int CoronaMemoryIsReadable (lua_State * L, int objectIndex)
{
	return lua_isstring(L, objectIndex) || FindResult(L, objectIndex, 0, false);
}

CORONA_API
int CoronaMemoryIsWritable (lua_State * L, int objectIndex)
{
	return FindResult(L, objectIndex, 1, false);
}

CORONA_API
int CoronaMemoryIsResizableForReads (lua_State * L, int objectIndex)
{
	return FindResult(L, objectIndex, false, true);
}

CORONA_API
int CoronaMemoryIsResizableForWrites (lua_State * L, int objectIndex)
{
	return FindResult(L, objectIndex, true, true);
}

static void * GetCallbacksKey (lua_State * L, int objectIndex, bool writable)
{
	if (GetAssignedCallbacks(L, objectIndex, writable, false)) // ...[, callbacks]
	{
		return lua_touserdata(L, -1);
	}

	else
	{
		return nullptr;
	}
}

CORONA_API
void * CoronaMemoryGetReadCallbacks (lua_State * L, int objectIndex)
{
	void * key = GetCallbacksKey(L, objectIndex, 0); // ...[, read_callbacks]

	if (!key && lua_isstring(L, objectIndex))
	{
		GetLuaObjectReader(L); // ..., LuaObjectReader

		key = lua_touserdata(L, -1);
	}

	return key;
}

CORONA_API
void * CoronaMemoryGetWriteCallbacks (lua_State * L, int objectIndex)
{
	return GetCallbacksKey(L, objectIndex, 1); // ...[, write_callbacks]
}