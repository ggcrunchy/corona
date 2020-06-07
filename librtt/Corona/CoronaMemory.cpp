//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Array.h"

#include "CoronaMemory.h"
#include "CoronaLua.h"

struct CoronaMemoryData {
	CoronaMemoryData();
	CoronaMemoryData(const CoronaMemoryData & other) = default;

	void * fBytes;
	unsigned int fAlignment;
	unsigned int fByteCount;
	unsigned int fStrideCount;
	int fLastKnownStackPosition; // where we think data is; if this gets out of sync we scour the stack for the first full userdata that resolves to 'this'
};

CoronaMemoryData::CoronaMemoryData () :
	fBytes(nullptr),
	fAlignment(0U),
	fByteCount(0U),
	fStrideCount(0U),
	fLastKnownStackPosition(0)
{
}

static bool IsSelf (lua_State * L, int index, const CoronaMemoryData * self)
{
	return lua_type(L, index) == LUA_TUSERDATA && lua_topointer(L, index) == self;
}

CORONA_API
int CoronaMemoryGetPosition (lua_State * L, CoronaMemoryData * memoryData)
{
	if (!memoryData || 0 == memoryData->fLastKnownStackPosition)
	{
		return -1;
	}

	if (!IsSelf(L, memoryData->fLastKnownStackPosition, memoryData))
	{
		memoryData->fLastKnownStackPosition = 0;

		for (int i = 1, top = lua_gettop(L); i <= top; ++i)
		{
			if (IsSelf(L, i, memoryData))
			{
				memoryData->fLastKnownStackPosition = i;

				break;
			}
		}
	}

	return memoryData->fLastKnownStackPosition;
}

CORONA_API
int CoronaMemoryPushValue (lua_State * L, CoronaMemoryData * memoryData)
{
	if (CoronaMemoryGetPosition(L, memoryData))
	{
		lua_pushvalue(L, memoryData->fLastKnownStackPosition); // ..., memoryData

		return 1;
	}

	return 0;
}

static void GetWeakTable (lua_State * L, const char * which)
{
	static int nonce;

	lua_pushlightuserdata(L, &nonce); // ..., nonce
	lua_rawget(L, LUA_REGISTRYINDEX); // ..., weak_tables?

	if (lua_isnil(L, -1))
	{
		lua_pop(L, 1); // ...
		lua_createtable(L, 2, 0); // ..., weak_tables
		lua_newtable(L); // ..., weak_tables, readers
		lua_newtable(L); // ..., weak_tables, readers, reader_writers
		lua_newtable(L); // ..., weak_tables, readers, reader_writers, writers
		lua_createtable(L, 0, 1); // ..., weak_tables, readers, reader_writers, writers, mt
		lua_pushliteral(L, "k"); // ..., weak_tables, readers, reader_writers, writers, mt, "k"
		lua_setfield(L, -2, "__mode"); // ..., weak_tables, readers, reader_writers, writers, mt = { __mode = "k" }
		lua_pushvalue(L, -1); // ..., weak_tables, readers, reader_writers, writers, mt, mt
		lua_pushvalue(L, -1); // ..., weak_tables, readers, reader_writers, writers, mt, mt, mt
		lua_setmetatable(L, -4); // ..., weak_tables, readers, reader_writers, writers, mt, mt; writers.metatable = mt
		lua_setmetatable(L, -4); // ..., weak_tables, readers, reader_writers, writers, mt; reader_writers.metatable = mt
		lua_setmetatable(L, -4); // ..., weak_tables, readers, reader_writers, writers; readers.metatable = mt
		lua_setfield(L, -4, "writers"); // ..., weak_tables = { writers = writers }, readers, reader_writers
		lua_setfield(L, -3, "reader_writers"); // ..., weak_tables = { reader_writers = reader_writers, writers }, readers
		lua_setfield(L, -2, "readers"); // ..., weak_tables = { readers = readers, reader_writers, writers }
		lua_pushlightuserdata(L, &nonce); // ..., weak_tables, nonce
		lua_pushvalue(L, -2); // ..., weak_tables, nonce, weak_tables
		lua_rawset(L, LUA_REGISTRYINDEX); // ..., weak_tables; registry[nonce] = weak_tables
	}

	lua_getfield(L, -1, which); // ..., weak_tables, wt
	lua_remove(L, -2); // ..., wt
}

static void CallbackLookup (lua_State * L, int objectIndex, const char * kind, bool wantEnsureSize)
{
	GetWeakTable(L, kind); // ..., wt

	lua_pushvalue(L, objectIndex); // ..., wt, object
	lua_rawget(L, -2); // ..., wt, callback?

	if (wantEnsureSize && !((CoronaMemoryCallbacks *)lua_touserdata(L, -1))->ensureSize)
	{
		lua_pushnil(L); // ..., wt, callback?, nil
		lua_remove(L, -2); // ..., wt, nil
	}
}

static bool FindCallbacks (lua_State * L, int & objectIndex, const char * preferred, const char * alternate, bool wantEnsureSize)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	int top = lua_gettop(L);

	CallbackLookup(L, objectIndex, preferred, wantEnsureSize); // ..., preferred, callback?

	if (lua_isnil(L, -1) && alternate)
	{
		CallbackLookup(L, objectIndex, alternate, wantEnsureSize); // ..., preferred, callback?, alternate, callback?
	}

	bool found = !lua_isnil(L, -1);

	if (found)
	{
		++top;

		lua_insert(L, top); // ..., callback, preferred[, callback?[, alternate]]
	}

	lua_settop(L, top); // ...[, callback]

	return found;
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

			lua_pop(L, 3); // ..., t
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

static CoronaMemoryData * GetBytesAndRest (lua_State * L, int objectIndex, int oldTop, const CallbackInfo & info, CoronaMemoryExtra * extra)
{
	CoronaMemoryData work;

	work.fBytes = info.fCallbacks->getBytes(L, objectIndex, &work.fByteCount, extra); // ...[, etc., error]

	if (!work.fBytes)
	{
		ReplaceWithError(L, objectIndex, oldTop, "'getBytes()' callback failed");

		return nullptr;
	}

	if (info.fCallbacks->getAlignment && !info.fCallbacks->getAlignment(L, objectIndex, &work.fAlignment, extra)) // ...[, etc., error]
	{
		ReplaceWithError(L, objectIndex, oldTop, "'getAlignment()' callback failed"); // ...

		return nullptr;
	}

	lua_settop(L, oldTop); // ...

	if (info.fCallbacks->getStrides && !info.fCallbacks->getStrides(L, objectIndex, &work.fStrideCount, extra)) // ...[, etc. / strides, error]
	{
		ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback failed"); // ...

		return nullptr;
	}

	if (work.fStrideCount > 0U)
	{
		if (oldTop + work.fStrideCount > lua_gettop(L))
		{
			ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback reports more strides than found on stack"); // ...

			return nullptr;
		}

		unsigned strideProduct = 1U;

		for (int i = 1; i <= work.fStrideCount; ++i)
		{
			if (!lua_isnumber(L, -i))
			{
				ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback returned non-numeric stride"); // ...

				return nullptr;
			}

			strideProduct *= (unsigned int)lua_tointeger(L, -i);
		}

		if (strideProduct > work.fByteCount)
		{
			ReplaceWithError(L, objectIndex, oldTop, "'getStrides()' callback returns stride spanning more bytes than received"); // ...

			return nullptr;
		}
	}

	lua_pushvalue(L, objectIndex); // ..., object, ...[, etc., strides], object

	CoronaMemoryData * memoryData = GetMemoryData(L, work); // ..., object, ...[, etc., strides], object, memoryData

	lua_replace(L, objectIndex); // ..., memoryData, ...[, etc., strides], object

	memoryData->fLastKnownStackPosition = objectIndex;

	CoronaMemoryAddToStash(L, memoryData); // ..., memoryData, ...[, etc., strides]

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

	return memoryData;
}

static CoronaMemoryData * AuxAcquire (lua_State * L, int objectIndex, void * param)
{
	CallbackInfo info(L); // ...

	CoronaMemoryExtra extra = {param, info.fUserData};
	int oldTop = lua_gettop(L);

	if (BeforeGetBytes(L, objectIndex, oldTop, info, &extra))
	{
		return GetBytesAndRest(L, objectIndex, oldTop, info, &extra);
	}

	else
	{
		return nullptr;
	}
}

CORONA_API
CoronaMemoryData * CoronaMemoryAcquireReadableBytes (lua_State * L, int objectIndex, void * param)
{
	if (FindCallbacks(L, objectIndex, "readers", "reader_writers", false)) // ..., object, ...[, reader]
	{
		return AuxAcquire(L, objectIndex, param);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader or reader-writer");

		return nullptr;
	}
}

CORONA_API
CoronaMemoryData * CoronaMemoryAcquireWritableBytes (lua_State * L, int objectIndex, void * param)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (FindCallbacks(L, objectIndex, "writers", "reader_writers", false)) // ..., object, ...[, writer]
	{
		return AuxAcquire(L, objectIndex, param);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader-writer");

		return nullptr;
	}
}

static CoronaMemoryData * AuxEnsureSizeAndAcquire (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param)
{
	CallbackInfo info(L); // ...

	CoronaMemoryExtra extra = {param, info.fUserData};
	int oldTop = lua_gettop(L);

	if (!info.fCallbacks->ensureSize)
	{
		ReplaceWithError(L, objectIndex, oldTop, "Missing 'ensureSize()' callback");
	}

	else if (BeforeGetBytes(L, objectIndex, oldTop, info, &extra))
	{
		if (!info.fCallbacks->ensureSize(L, objectIndex, expectedSizes, sizeCount, &extra)) // ...[, etc., error]
		{
			ReplaceWithError(L, objectIndex, oldTop, "'ensureSize()' callback failed"); // ...
		}

		else
		{
			return GetBytesAndRest(L, objectIndex, oldTop, info, &extra);
		}
	}

	return nullptr;
}

CORONA_API
CoronaMemoryData * CoronaMemoryEnsureSizeAndAcquireReadableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (FindCallbacks(L, objectIndex, "readers", "reader_writers", true)) // ..., object, ...[, reader]
	{
		return AuxEnsureSizeAndAcquire(L, objectIndex, expectedSizes, sizeCount, param);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader or reader-writer");

		return nullptr;
	}
}

CORONA_API
CoronaMemoryData * CoronaMemoryEnsureSizeAndAcquireWritableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (FindCallbacks(L, objectIndex, "writers", "reader_writers", true)) // ..., object, ...[, writer]
	{
		return AuxEnsureSizeAndAcquire(L, objectIndex, expectedSizes, sizeCount, param);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no writer or reader-writer");

		return nullptr;
	}
}

CORONA_API
void * CoronaMemoryGetBytes (lua_State * L, CoronaMemoryData * memoryData)
{
	return CoronaMemoryGetPosition(L, memoryData) ? memoryData->fBytes : nullptr;
}

CORONA_API
unsigned int CoronaMemoryGetByteCount (lua_State * L, CoronaMemoryData * memoryData)
{
	return CoronaMemoryGetPosition(L, memoryData) ? memoryData->fByteCount : 0U;
}

CORONA_API
int CoronaMemoryGetStride (lua_State * L, CoronaMemoryData * memoryData, int strideIndex, unsigned int * stride)
{
	if (CoronaMemoryGetPosition(L, memoryData) && strideIndex >= 1 && strideIndex <= memoryData->fStrideCount)
	{
		lua_getfenv(L, memoryData->fLastKnownStackPosition); // ..., env
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
unsigned int CoronaMemoryGetStrideCount (lua_State * L, CoronaMemoryData * memoryData)
{
	return CoronaMemoryGetPosition(L, memoryData) ? memoryData->fStrideCount : 0U;
}

CORONA_API
unsigned int CoronaMemoryGetAlignment (lua_State * L, CoronaMemoryData * memoryData)
{
	if (CoronaMemoryGetPosition(L, memoryData))
	{
		return memoryData->fAlignment != 0U ? memoryData->fAlignment : 4U; // TODO: check for minimum?
	}

	return 0U;
}

static int StackSize (lua_State * L, int index)
{
	return (int)lua_objlen(L, index);
}

CORONA_API
int CoronaMemoryRelease (lua_State * L, CoronaMemoryData * memoryData)
{
	if (CoronaMemoryGetPosition(L, memoryData))
	{
		GetMemoryDataCache(L); // ..., memoryData, ..., cache

		lua_pushvalue(L, memoryData->fLastKnownStackPosition); // ..., memoryData, ..., cache, memoryData
		lua_rawseti(L, -2, StackSize(L, -1) + 1); // ..., memoryData, ..., cache = { ..., memoryData }
		lua_getfenv(L, memoryData->fLastKnownStackPosition); // ..., memoryData, ..., cache, env

		for (int n = (int)lua_objlen(L, -1); n > 0; --n)
		{
			lua_pushnil(L); // ..., memoryData, ..., cache, env, nil
			lua_rawseti(L, -2, n); // ..., memoryData, ..., cache, env = { ..., [#env] = nil }
		}

		lua_pop(L, 2); // ..., memoryData, ...
		lua_pushliteral(L, "done"); // ..., memoryData, ..., "done"
		lua_replace(L, memoryData->fLastKnownStackPosition); // ..., "done", ...

		memoryData->fLastKnownStackPosition = 0;

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaMemoryAddToStash (lua_State * L, CoronaMemoryData * memoryData)
{
	if (CoronaMemoryGetPosition(L, memoryData) && memoryData->fLastKnownStackPosition != lua_gettop(L))
	{
		lua_getfenv(L, memoryData->fLastKnownStackPosition); // ..., object, env
		lua_insert(L, -2); // ..., env, object
		lua_rawseti(L, -2, StackSize(L, -2) + 1); // ..., env = { ..., object }
		lua_pop(L, 1); // ...

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaMemoryRemoveFromStash (lua_State * L, CoronaMemoryData * memoryData)
{
	if (CoronaMemoryGetPosition(L, memoryData))
	{
		lua_getfenv(L, memoryData->fLastKnownStackPosition); // ..., env

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
int CoronaMemoryPopFromStash (lua_State * L, CoronaMemoryData * memoryData)
{
	if (CoronaMemoryGetPosition(L, memoryData))
	{
		lua_getfenv(L, memoryData->fLastKnownStackPosition); // ..., env

		int size = StackSize(L, -1);

		if (size > 0)
		{
			lua_rawgeti(L, -1, size); // ..., env, object
			lua_pushnil(L); // ..., env, object, nil
			lua_rawseti(L, -3, size); // ..., env = { ..., [#env] = nil }, object
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

static CoronaMemoryCallbacks * NewCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, int writable, const char * name, void * userData)
{
	CoronaMemoryCallbacks * res = (CoronaMemoryCallbacks *)lua_newuserdata(L, sizeof(CoronaMemoryCallbacks)); // ..., callbacks

	*res = *callbacks;

	if (writable || name || userData)
	{
		lua_createtable(L, 0, 4); // ..., callbacks, callbacks_info

		if (name)
		{
			lua_pushstring(L, name); // ..., callbacks, callbacks_info, name
			lua_setfield(L, -2, "name"); // ..., callbacks, callbacks_info = { name = name }
		}

		if (userData)
		{
			lua_pushlightuserdata(L, userData); // ..., callbacks, callbacks_info, userData
			lua_setfield(L, -2, "userData"); // ..., callbacks, callbacks_info = { name?, userData = userData }
		}

		lua_insert(L, -2); // ..., callbacks_info, callbacks
		lua_setfield(L, -2, "callbacks"); // ..., callbacks_info = { callbacks = callbacks, name?, userData? }
		lua_pushboolean(L, writable); // ..., callbacks_info, writable
		lua_setfield(L, -2, "writable"); // callbacks_info = { callbacks, name?, userData?, writable = writable }
	}

	return res;
}

static void GetCallbacksTable (lua_State * L)
{
	static int nonce;

	GetRegistryTable(L, &nonce); // ..., callbacks
}

CORONA_API
void * CoronaMemoryRegisterCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, int writable, const char * name, void * userData)
{
	CoronaMemoryCallbacks * res = nullptr;

	if (sizeof(CoronaMemoryCallbacks) == callbacks->size && callbacks->getBytes)
	{
		GetCallbacksTable(L); // ..., callbacks

		res = NewCallbacks(L, callbacks, writable, name, userData); // ..., callbacks, callbacks_info

		lua_pushlightuserdata(L, res); // ..., callbacks, callbacks_info, callbacks_ptr
		lua_insert(L, -2); // ..., callbacks, callbacks_ptr, callbacks_info
		lua_rawset(L, -3); // ..., callbacks = { ..., [callbacks_ptr] = callbacks_info }
		lua_pop(L, 1); // ...
	}

	return res;
}

CORONA_API
void * CoronaMemoryFindRegisteredCallbacks (lua_State * L, const char * name)
{
	int top = lua_gettop(L);
	void * key = nullptr;

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

	return key;
}

static void ObjectInfoLookup (lua_State * L, int objectIndex, int writable)
{
	GetWeakTable(L, writable ? "reader_writers" : "readers"); // ..., object, ..., wt

	lua_pushvalue(L, objectIndex); // ..., object, ..., wt, object
	lua_rawget(L, -2); // ..., object, ..., wt, info?
}

static int AssignInfoToObject (lua_State * L, int objectIndex, int writable)
{
	ObjectInfoLookup(L, objectIndex, writable);	// ..., object, ..., callbacks_info, wt, info?

	if (lua_isnil(L, -1))
	{
		lua_pushvalue(L, objectIndex); // ..., object, ..., callbacks, callbacks_info, wt, nil, object
		lua_pushvalue(L, -4); // ..., object, ..., callbacks, callbacks_info, wt, nil, object, callbacks_info
		lua_rawset(L, -4); // ..., object, ..., callbacks, callbacks_info, wt = { ..., [object] = callbacks_info }, nil

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaMemorySetRegisteredCallbacks (lua_State * L, int objectIndex, void * key)
{
	int available = 0;

	if (!lua_isnoneornil(L, objectIndex))
	{
		objectIndex = CoronaLuaNormalize(L, objectIndex);

		int top = lua_gettop(L);

		GetCallbacksTable(L); // ..., object, ..., callbacks

		lua_pushlightuserdata(L, key); // ..., object, ..., callbacks, key
		lua_rawget(L, -2); // ..., object, ..., callbacks, callbacks_info?

		if (!lua_isnil(L, -1))
		{
			int writable = 0;

			if (lua_istable(L, -1))
			{
				lua_getfield(L, -1, "writable"); // ..., object, ..., callbacks, callbacks_info, writable?

				writable = lua_toboolean(L, -1);

				lua_pop(L, 1); // ..., object, ..., callbacks, callbacks_info
			}

			available = AssignInfoToObject(L, objectIndex, writable); // ..., object, ..., callbacks, callbacks_info, wt, info?
		}

		lua_settop(L, top); // ..., object, ...
	}

	return available;
}

CORONA_API
int CoronaMemorySetCallbacks (lua_State * L, int objectIndex, const CoronaMemoryCallbacks * callbacks, int writable, void * userData)
{
	if (!lua_isnoneornil(L, objectIndex) && sizeof(CoronaMemoryCallbacks) == callbacks->size && callbacks->getBytes)
	{
		objectIndex = CoronaLuaNormalize(L, objectIndex);

		CoronaMemoryCallbacks * res = NewCallbacks(L, callbacks, writable, nullptr, userData); // ..., callbacks_info
		int available = AssignInfoToObject(L, objectIndex, writable); // ..., callbacks_info, wt, info?

		lua_pop(L, 3); // ...

		return available;
	}

	return 0;
}

CORONA_API
int CoronaMemoryIsReadable (lua_State * L, int objectIndex)
{
	if (FindCallbacks(L, objectIndex, "readers", "reader_writers", false)) // ...[, reader]
	{
		lua_pop(L, 1); // ...

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaMemoryIsResizable (lua_State * L, int objectIndex, int writable)
{
	if (FindCallbacks(L, objectIndex, writable ? "writers" : "readers", "reader_writers", 0)) // ...[, callbacks]
	{
		CoronaMemoryCallbacks * callbacks = (CoronaMemoryCallbacks *)lua_touserdata(L, -1);

		lua_pop(L, 1); // ...

		if (callbacks->ensureSize)
		{
			return 1;
		}
	}

	return 0;
}

CORONA_API
int CoronaMemoryIsWritable (lua_State * L, int objectIndex)
{
	if (FindCallbacks(L, objectIndex, "writers", "reader_writers", false)) // ...[, writer]
	{
		lua_pop(L, 1); // ...

		return 1;
	}

	return 0;
}

static int Uses (lua_State * L, int objectIndex, const char * kind, const void * key)
{
	if (FindCallbacks(L, objectIndex, kind, nullptr, false)) // ...[, callbacks]
	{
		int uses = lua_touserdata(L, -1) == key;

		lua_pop(L, 1); // ...

		return uses;
	}

	return 0;
}

CORONA_API
int CoronaMemoryUsesReader (lua_State * L, int objectIndex, const void * key)
{
	return Uses(L, objectIndex, "readers", key);
}

CORONA_API
int CoronaMemoryUsesReaderWriter (lua_State * L, int objectIndex, const void * key)
{
	return Uses(L, objectIndex, "reader_writers", key);
}

CORONA_API
int CoronaMemoryUsesWriter (lua_State * L, int objectIndex, const void * key)
{
	return Uses(L, objectIndex, "writers", key);
}