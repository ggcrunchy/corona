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

static bool IsSelf (lua_State * L, int index, const CoronaMemoryData * self)
{
	return lua_type(L, index) == LUA_TUSERDATA && lua_topointer(L, index) == self;
}

CORONA_API
int CoronaMemoryGetPosition (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	if (!memoryHandle || 0 == memoryHandle->lastKnownStackPosition || !memoryHandle->data)
	{
		return 0;
	}

	if (!IsSelf(L, memoryHandle->lastKnownStackPosition, memoryHandle->data))
	{
		memoryHandle->lastKnownStackPosition = 0;

		for (int i = 1, top = lua_gettop(L); i <= top; ++i)
		{
			if (IsSelf(L, i, memoryHandle->data))
			{
				memoryHandle->lastKnownStackPosition = i;

				break;
			}
		}
	}

	return memoryHandle->lastKnownStackPosition;
}

static void GetWeakTable (lua_State * L, CoronaMemoryAcquireKind which)
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

		for (int i = 1; i <= 3; ++i) // cf. CoronaMemoryCallbackKind
		{
			lua_newtable(L); // ..., weak_table, mt, wt

			if (i < 3)
			{
				lua_pushvalue(L, -2); // ..., weak_tables, mt, wt, mt
			}

			else
			{
				lua_insert(L, -2); // ..., weak_tables, wt, mt
			}

			lua_setmetatable(L, -2); // ..., weak_tables[, mt], wt; wt.metatable = mt
			lua_rawseti(L, i < 3 ? -3 : -2, i); // ..., weak_tables = { ..., wt }[, mt]
		}

		lua_pushlightuserdata(L, &nonce); // ..., weak_tables, nonce
		lua_pushvalue(L, -2); // ..., weak_tables, nonce, weak_tables
		lua_rawset(L, LUA_REGISTRYINDEX); // ..., weak_tables; registry[nonce] = weak_tables
	}

	lua_rawgeti(L, -1, int(which)); // ..., weak_tables, wt
	lua_remove(L, -2); // ..., wt
}

static void CallbackLookup (lua_State * L, int objectIndex, CoronaMemoryAcquireKind kind, bool wantEnsureSizes)
{
	GetWeakTable(L, kind); // ..., wt

	lua_pushvalue(L, objectIndex); // ..., wt, object
	lua_rawget(L, -2); // ..., wt, callback?

	if (wantEnsureSizes && !((CoronaMemoryCallbacks *)lua_touserdata(L, -1))->ensureSizes)
	{
		lua_pushnil(L); // ..., wt, callback?, nil
		lua_remove(L, -2); // ..., wt, nil
	}
}

static bool FindCallbacks (lua_State * L, int & objectIndex, CoronaMemoryAcquireKind preferred, CoronaMemoryAcquireKind alternate, bool wantEnsureSizes)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	int top = lua_gettop(L);

	CallbackLookup(L, objectIndex, preferred, wantEnsureSizes); // ..., preferred, callback?

	if (lua_isnil(L, -1) && alternate)
	{
		CallbackLookup(L, objectIndex, alternate, wantEnsureSizes); // ..., preferred, callback?, alternate, callback?
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

static CoronaMemoryHandle GetBytesAndRest (lua_State * L, int objectIndex, int oldTop, const CallbackInfo & info, int writable, CoronaMemoryExtra * extra)
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

	memoryData->fWritable = !!writable;

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

static CoronaMemoryHandle AuxAcquire (lua_State * L, int objectIndex, int writable, void * param)
{
	CallbackInfo info(L); // ...

	CoronaMemoryExtra extra = {param, info.fUserData};
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

		void * key = CoronaMemoryRegisterCallbacks(L, &callbacks, nullptr, nullptr);

		lua_pushlightuserdata(L, &nonce); // ..., callbacks, nonce
		lua_pushlightuserdata(L, key); // ..., callbacks, nonce, key
		lua_rawset(L, LUA_REGISTRYINDEX); // ..., callbacks; registry[nonce] = key
		lua_pushlightuserdata(L, key); // ..., callbacks, key
	}

	lua_rawget(L, -2); // ..., callbacks, LuaObjectReader
	lua_remove(L, -2); // ..., LuaObjectReader
}

CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireReadableBytes (lua_State * L, int objectIndex, void * param)
{
	bool found = FindCallbacks(L, objectIndex, kMemoryAcquireKind_ReadOnly, kMemoryAcquireKind_ReadWrite, false); // ..., object, ...[, reader]

	if (!found && lua_isstring(L, objectIndex))
	{
		GetLuaObjectReader(L); // ..., object, ..., LuaObjectReader

		found = true;
	}

	if (found)
	{
		return AuxAcquire(L, objectIndex, 0, param);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader or reader-writer");

		return NullHandle();
	}
}

CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireWritableBytes (lua_State * L, int objectIndex, void * param)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (FindCallbacks(L, objectIndex, kMemoryAcquireKind_WriteOnly, kMemoryAcquireKind_ReadWrite, false)) // ..., object, ...[, writer]
	{
		return AuxAcquire(L, objectIndex, 1, param);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader-writer");

		return NullHandle();
	}
}

static CoronaMemoryHandle AuxEnsureSizeAndAcquire (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, int writable, void * param)
{
	CallbackInfo info(L); // ...

	CoronaMemoryExtra extra = {param, info.fUserData};
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
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireReadableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (FindCallbacks(L, objectIndex, kMemoryAcquireKind_ReadOnly, kMemoryAcquireKind_ReadWrite, false)) // ..., object, ...[, reader]
	{
		return AuxEnsureSizeAndAcquire(L, objectIndex, expectedSizes, sizeCount, 0, param);
	}

	else
	{
		ReplaceWithError(L, objectIndex, lua_gettop(L), "Object has no reader or reader-writer");

		return NullHandle();
	}
}

CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireWritableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param)
{
	objectIndex = CoronaLuaNormalize(L, objectIndex);

	if (FindCallbacks(L, objectIndex, kMemoryAcquireKind_WriteOnly, kMemoryAcquireKind_ReadWrite, false)) // ..., object, ...[, writer]
	{
		return AuxEnsureSizeAndAcquire(L, objectIndex, expectedSizes, sizeCount, 1, param);
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
int CoronaMemoryPopFromStash (lua_State * L, CoronaMemoryHandle * memoryHandle)
{
	if (CoronaMemoryGetPosition(L, memoryHandle))
	{
		lua_getfenv(L, memoryHandle->lastKnownStackPosition); // ..., env

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

static CoronaMemoryCallbacks * NewCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, void * userData)
{
	CoronaMemoryCallbacks * res = (CoronaMemoryCallbacks *)lua_newuserdata(L, sizeof(CoronaMemoryCallbacks)); // ..., callbacks

	*res = *callbacks;

	if (name || userData)
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
	}

	return res;
}

CORONA_API
void * CoronaMemoryRegisterCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, void * userData)
{
	CoronaMemoryCallbacks * res = nullptr;

	if (sizeof(CoronaMemoryCallbacks) == callbacks->size && callbacks->getBytes)
	{
		GetCallbacksTable(L); // ..., callbacks

		res = NewCallbacks(L, callbacks, name, userData); // ..., callbacks, callbacks_info

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

static void ObjectInfoLookup (lua_State * L, int objectIndex, CoronaMemoryAcquireKind kind)
{
	GetWeakTable(L, kind); // ..., object, ..., wt

	lua_pushvalue(L, objectIndex); // ..., object, ..., wt, object
	lua_rawget(L, -2); // ..., object, ..., wt, info?
}

static int AssignInfoToObject (lua_State * L, int objectIndex, CoronaMemoryAcquireKind kind)
{
	ObjectInfoLookup(L, objectIndex, kind);	// ..., object, ..., callbacks_info, wt, info?

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
int CoronaMemorySetRegisteredCallbacks (lua_State * L, int objectIndex, CoronaMemoryAcquireKind kind, void * key)
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
			available = AssignInfoToObject(L, objectIndex, kind); // ..., object, ..., callbacks, callbacks_info, wt, info?
		}

		lua_settop(L, top); // ..., object, ...
	}

	return available;
}

CORONA_API
int CoronaMemorySetCallbacks (lua_State * L, int objectIndex, const CoronaMemoryCallbacks * callbacks, CoronaMemoryAcquireKind kind, void * userData)
{
	if (!lua_isnoneornil(L, objectIndex) && sizeof(CoronaMemoryCallbacks) == callbacks->size && callbacks->getBytes)
	{
		objectIndex = CoronaLuaNormalize(L, objectIndex);

		CoronaMemoryCallbacks * res = NewCallbacks(L, callbacks, nullptr, userData); // ..., callbacks_info
		int available = AssignInfoToObject(L, objectIndex, kind); // ..., callbacks_info, wt, info?

		lua_pop(L, 3); // ...

		return available;
	}

	return 0;
}


CORONA_API
int CoronaMemoryGetAlignment (lua_State * L, int objectIndex, int writable, unsigned int * alignment)
{
	CoronaMemoryAcquireKind preferred = writable ? kMemoryAcquireKind_WriteOnly : kMemoryAcquireKind_ReadOnly;

	if (FindCallbacks(L, objectIndex, preferred, kMemoryAcquireKind_ReadWrite, false)) // ...[, callbacks]
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

static int FindResult (lua_State * L, int objectIndex, CoronaMemoryAcquireKind preferred, CoronaMemoryAcquireKind alternate, bool wantEnsureSize)
{
	if (FindCallbacks(L, objectIndex, preferred, alternate, wantEnsureSize)) // ...[, callbacks]
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
	return lua_isstring(L, objectIndex) || FindResult(L, objectIndex, kMemoryAcquireKind_ReadOnly, kMemoryAcquireKind_ReadWrite, false);
}

CORONA_API
int CoronaMemoryIsResizable (lua_State * L, int objectIndex, int writable)
{
	CoronaMemoryAcquireKind preferred = writable ? kMemoryAcquireKind_WriteOnly : kMemoryAcquireKind_ReadOnly;

	return FindResult(L, objectIndex, preferred, kMemoryAcquireKind_ReadWrite, true);
}

CORONA_API
int CoronaMemoryIsWritable (lua_State * L, int objectIndex)
{
	return FindResult(L, objectIndex, kMemoryAcquireKind_WriteOnly, kMemoryAcquireKind_ReadWrite, false);
}

static void * GetKey (lua_State * L, int objectIndex, CoronaMemoryAcquireKind kind)
{
	if (FindCallbacks(L, objectIndex, kind, kMemoryAcquireKind_Undefined, false)) // ...[, callbacks]
	{
		void * key = lua_touserdata(L, -1);

		lua_pop(L, 1); // ...

		return key;
	}

	return nullptr;
}

CORONA_API
void * CoronaMemoryGetReadOnlyKey (lua_State * L, int objectIndex)
{
	void * key = GetKey(L, objectIndex, kMemoryAcquireKind_ReadOnly);

	if (!key && lua_isstring(L, objectIndex))
	{
		GetLuaObjectReader(L); // ..., LuaObjectReader

		key = lua_touserdata(L, -1);

		lua_pop(L, 1); // ...
	}

	return key;
}

CORONA_API
void * CoronaMemoryGetReadWriteKey (lua_State * L, int objectIndex)
{
	return GetKey(L, objectIndex, kMemoryAcquireKind_ReadWrite);
}

CORONA_API
void * CoronaMemoryGetWriteOnlyKey (lua_State * L, int objectIndex)
{
	return GetKey(L, objectIndex, kMemoryAcquireKind_WriteOnly);
}