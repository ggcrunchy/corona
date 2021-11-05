//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaMemory_H__
#define _CoronaMemory_H__

#include "CoronaMacros.h"


#ifdef __cplusplus
extern "C" {
#endif
	typedef struct lua_State lua_State;
#ifdef __cplusplus
}
#endif

/**
 Extra information supplied to the various callbacks during an acquire.
*/
typedef struct CoronaMemoryExtra {
	/**
	 If present, the parameters to the acquire operation.
	*/
	void * params;

	/**
	 If present, the data associated with the callback when registered or set.
	*/
	void * userData;
} CoronaMemoryExtra;

/**
 TODO
*/
typedef enum {
    /**
     TODO
    */
    kMemoryRead,

    /**
     TODO
    */
    kMemoryWrite
} CoronaMemoryKind;

/**
 This structure contains callbacks used to acquire readable or writable access to memory, given an object
 on the stack. The object itself will only be modified by its callbacks, if at all.
 The callbacks are traversed in the order listed and called if present / relevant, stopping if one fails,
 i.e. return `NULL` or `0`.
 On a successful attempt, some internal state will replace the object on the stack. However, a reference
 to the object is still maintained.
 In the event of failure, an error will the replace the object on the stack. If the stack has grown, the
 topmost item--if not `nil`--will be interpreted as this error; otherwise, a suitable default is used.
*/
typedef struct CoronaMemoryCallbacks {
	/**
	 Required
	 When creating instance of this type set this member to `size = sizeof(CoronaMemoryCallbacks)`.
	 This is required for identifying version of API used.
	*/
	unsigned long size;

	/**
	 Optional
	 Power-of-2 minimum alignment to expect for bytes. If 0, use the default.
	*/
	unsigned int alignment;

	/**
	 Optional
	 User-supplied data to pass along to the various callbacks. @see CoronaMemoryExtra
	 Users can assign this explicitly. Alternatively, the callbacks can assume ownership of a value on the Lua
	 stack, cf. the register / set callbacks functions' `userDataFromStack` commentary.
	*/
	void * userData;

	/**
	 Optional
	 The acquire attempt will be aborted if this returns 0.
	*/
	int (*canAcquire)( lua_State * L, int objectIndex, CoronaMemoryExtra * extra );

	/**
	 Optional
	 If requested, attempts to conform the object's data to the expected sizes.
	*/
	int (*ensureSizes)( lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, CoronaMemoryExtra * extra );

	/**
	 Required
	 On success, points to the acquired memory and populates `byteCount` with the addressable size; if the stack has grown,
	 a reference to the topmost value will also be kept with the memory data.
	*/
	void * (*getBytes)( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int * byteCount, CoronaMemoryExtra * extra );

	/**
	 Optional
	 If this returns non-0, `strideCount` is populated with some count and the corresponding numbers pushed on the stack.
	*/
	int (*getStrides)( lua_State * L, int objectIndex, unsigned int * strideCount, CoronaMemoryExtra * extra );
} CoronaMemoryCallbacks;


// C API
// ----------------------------------------------------------------------------

/**
 Handle to acquired memory, used by several operations.
 These fields are internal and should not be used directly.
*/
typedef struct CoronaMemoryHandle {
	int lastKnownStackPosition;
	struct CoronaMemoryData * data;
} CoronaMemoryHandle;

/**
 Get the last error from a function taking a `CoronaMemoryHandle *`.
 Any error is cleared when calling such a function, possibly removing it from memory.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @param clear If non-0, clear any error after reading it.
 @return Read-only error string, or `NULL` if there was no new error.
*/
CORONA_API
const char * CoronaMemoryGetLastError( lua_State * L, CoronaMemoryHandle * memoryHandle, int clear ) CORONA_PUBLIC_SUFFIX;

/**
 Check if a memory handle is valid.
 Intended mainly to validate acquire results; the remaining functions that accept a `CoronaMemoryHandle *` do their own checks.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 if valid.
*/
CORONA_API
int CoronaMemoryIsValid( lua_State * L, const CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Intended mainly for internal use, in particular by the remaining functions that accept a `CoronaMemoryHandle *`.
 Gets the data's current stack position, performing a search if necessary.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Stack position:
		 0 - means the object was or has become invalid, e.g. was released or removed from the stack
		 otherwise - the up-to-date position
*/
CORONA_API
int CoronaMemoryGetPosition( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Acquire memory from an object, via its callbacks. @see CoronaMemoryCallbacks
 @param L Lua state pointer
 @param objectIndex Stack position of the object providing the memory. The call
			will replace this object: on success, by some internal state; otherwise, an error.
            If this is a read-style acquire, the object may be a Lua string: if so, and it has
            no callbacks, it is interpreted as a length-sized array.
 @param kind TODO
 @param params User-supplied parameter; may be `NULL`. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireBytes( lua_State * L, int objectIndex, CoronaMemoryKind kind, void * params ) CORONA_PUBLIC_SUFFIX;

/**
 Acquire memory from an object, via its callbacks. @see CoronaMemoryCallbacks
			This requires an `ensureSizes()` callback and will invoke it with the expected sizes.
 @param L Lua state pointer
 @param objectIndex Stack position of the object providing the memory. The call will replace this object: on success, by
			some internal state; otherwise, an error.
 @param kind
 @param expectedSizes Array of sizes.
 @param sizeCount Count of elements in `expectedSizes`, >= 1.
 @param params User-supplied parameter; may be `NULL`. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireBytes( lua_State * L, int objectIndex, CoronaMemoryKind kind, const unsigned int * expectedSizes, int sizeCount, void * params ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Get readable memory, following a corresponding acquire.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return On success, the memory; else `NULL`.
*/
CORONA_API
const void * CoronaMemoryGetReadableBytes( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Get writable memory, following a corresponding acquire.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return On success, the memory; else `NULL`.
*/
CORONA_API
void * CoronaMemoryGetWritableBytes( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Get the number of bytes acquired.
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return Count, >= 0.
*/
CORONA_API
unsigned int CoronaMemoryGetByteCount( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Get a stride corresponding to the acquired memory.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @param strideIndex Index of the stride, between 1 and the stride count, inclusive.
 @stride On success, populated with the requested stride.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryGetStride( lua_State * L, CoronaMemoryHandle * memoryHandle, int strideIndex, unsigned int * stride ) CORONA_PUBLIC_SUFFIX;

/**
 Get the number of strides corresponding to the acquired memory.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Count, >= 0.
*/
CORONA_API
unsigned int CoronaMemoryGetStrideCount( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Release the state used by the acquired memory. This is not strictly necessary (only the stack is used internally to maintain
 references, thus garbage collection will account for everything), but allows for some resource-pooling.
 On success, the handle is invalidated and replaced on the stack with `false`.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryRelease( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 The object on top of the stack is appended to an array belonging to the acquired memory.
 This is mainly intended for internal use: as described in the various acquire operations, the object will be replaced on the
 stack. It might not be safe to let the object simply be collected, say if it backs the acquired memory; furthermore, in some
 cases we might want to maintain intermediate components, with the same concerns applying to each.
 In the event of an error, the underlying array will be cleaned up along with the acquired memory.
 TODO: should this require a hash? written on first add, must be same for subsequent ones? (if so, how to expose?)
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryAddToStash( lua_State * L, CoronaMemoryHandle * memoryHandle/*, unsigned int * hash */) CORONA_PUBLIC_SUFFIX;

/**
 Remove the most recently added object from the acquired memory's stash. @see CoronaMemoryAddToStash
 TODO: if add requires hash, so must this
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryRemoveFromStash( lua_State * L, CoronaMemoryHandle * memoryHandle/*, unsigned int hash */ ) CORONA_PUBLIC_SUFFIX;

/**
 The object most recently added to the acquired memory's stash is pushed onto the stack. @see CoronaMemoryAddToStash
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryPeekAtStash( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Register callbacks for later use.
 @param L Lua state pointer
 @param callbacks @see CoronaMemoryCallbacks
 @param name If not `NULL`, used for later discovery. @see CoronaMemoryFindCallbacks
 @param userDataFromStack TODO @see CoronaMemoryCallbacks
 @return On success, the callbacks' key; else `NULL`.
*/
CORONA_API
void * CoronaMemoryRegisterCallbacks( lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, int userDataFromStack ) CORONA_PUBLIC_SUFFIX;

/**
 Find a set of registered callbacks. @see CoronaMemoryRegisterCallacks
 If multiple sets were registered under the same name, the first one found is chosen.
 @param L Lua state pointer
 @param name Name used to identify the set we want.
 @return On success, the callbacks' key (the same as returned during registration); else `NULL`.
*/
CORONA_API
void * CoronaMemoryFindCallbacks( lua_State * L, const char * name ) CORONA_PUBLIC_SUFFIX;

/**
 If available, push the object's callbacks onto the stack. @see CoronaMemorySetCallbacks, CoronaMemorySetRCallbacksByKey
 Unregistered callbacks must remain on the stack while setting any other callbacks with the returned key. @see CoronaMemoryRegisterCallbacks
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param kind
 @return On success, the callbacks' key; else `NULL`.
*/
CORONA_API
void * CoronaMemoryGetCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind ) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param kind
 @param callbacks @see CoronaMemoryCallbacks
 @param userData
 @return Non-0 on success.
*/
CORONA_API
void * CoronaMemorySetCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind, const CoronaMemoryCallbacks * callbacks, int userDataFromStack, unsigned int * hash ) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param kind
 @param key
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemorySetCallbacksByKey( lua_State * L, int objectIndex, CoronaMemoryKind kind, void * key, unsigned int * hash ) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param kind
 @param objectIndex Stack position of the object.
 @param kind
 @param callbacks @see CoronaMemoryCallbacks
 @param userData
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryRemoveCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int hash ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param kind
 @param alignment
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryGetAlignment( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int * alignment ) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param kind
 @return Non-0 if available, 0 otherwise.
*/
CORONA_API
int CoronaMemoryIsAvailable( lua_State * L, int objectIndex, CoronaMemoryKind kind ) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param kind 
 @return Non-0 if resizable, 0 otherwise.
*/
CORONA_API
int CoronaMemoryIsResizable( lua_State * L, int objectIndex, CoronaMemoryKind kind ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

#endif // _CoronaMemory_H__
