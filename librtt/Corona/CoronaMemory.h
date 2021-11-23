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
 What kind of memory operation or resource in being done or used?
*/
typedef enum {
    /**
     Memory is readable.
    */
    kMemoryRead,

    /**
     Memory is writeable.
    */
    kMemoryWrite
} CoronaMemoryKind;

/**
 This structure contains callbacks used to acquire access to memory, given an object on the stack.
 Solar has no understanding of this memory and will not modify it; only the callbacks may do so.
 The callbacks are traversed in the order listed below, and called if present and relevant. The
 acquire as a whole fails if any of these operations does, "failure" being a 0 or `NULL` result.
 After an acquire attempt, the object on the stack will be replaced.
 On success, some internal state will take the object's place, although a reference to it is held.[1]
 In the event of failure, on the other hand, an error will replace it. If the stack had grown, the
 topmost item--if not `nil`--will be used, else a suitable default.
 
 [1] - In particular, after `getBytes()` has succeeded. Up to this point, the callbacks may in fact
 swap out the object on the stack themselves. The reference is still available for collection, e.g.
 if `getStride()` were to raise an error.
*/
typedef struct CoronaMemoryCallbacks {
	/**
	 Required
	 When creating an instance of this type, set this member to `size = sizeof(CoronaMemoryCallbacks)`.
	 This is required to identify the API version used.
	*/
	unsigned long size;

	/**
	 Optional
	 Power-of-2 minimum alignment to expect for bytes. If 0, use the default.
	*/
	unsigned int alignment;

	/**
	 Optional
	 User-supplied data passed along to the various callbacks. @see CoronaMemoryExtra
	 Users can assign this explicitly. Alternatively, the callbacks may assume ownership of a value
     on the Lua stack, cf. the `userDataFromStack` commentary for the functions that register or
     set callbacks.
	*/
	void * userData;

	/**
	 Optional
	 The acquire attempt will be aborted if this returns 0.
	*/
	int (*canAcquire)( lua_State * L, int objectIndex, const CoronaMemoryExtra * extra );

	/**
	 Optional
	 If requested, attempts to conform the object's memory to the expected sizes.
	*/
	int (*ensureSizes)( lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, const CoronaMemoryExtra * extra );

	/**
	 Required
	 Point to the acquired memory and populate `byteCount` with the addressable size.
	*/
	void * (*getBytes)( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int * byteCount, const CoronaMemoryExtra * extra );

	/**
	 Optional
	 If requested, `strideCount` is populated with some count >= 0 and the corresponding numbers
     are pushed on the stack. Their product must be <= the byte count.
	*/
	int (*getStrides)( lua_State * L, int objectIndex, unsigned int * strideCount, const CoronaMemoryExtra * extra );
} CoronaMemoryCallbacks;


// C API
// ----------------------------------------------------------------------------

/**
 Handle to acquired memory, used by several operations.
 These fields are internal and not to be used directly.
*/
typedef struct CoronaMemoryHandle {
	int lastKnownStackPosition;
	struct CoronaMemoryData * data;
} CoronaMemoryHandle;

/**
 Get the last error from a function taking a `CoronaMemoryHandle *`.
 @param L Lua state pointer.
 @param memoryHandle Handle to memory data acquired from an object.
 @param clear If non-0, clear any error after reading it. (It remains in memory.)
 @return Read-only error string, or `NULL` if there was none.
*/
CORONA_API
const char * CoronaMemoryGetLastError( lua_State * L, CoronaMemoryHandle * memoryHandle, int clear ) CORONA_PUBLIC_SUFFIX;

/**
 Check if a memory handle is valid.
 If so, any error is cleared.
 Most APIs will do this on their own. However, it may be used to validate acquire results.
 @param L Lua state pointer.
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 if valid.
*/
CORONA_API
int CoronaMemoryIsValid( lua_State * L, const CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Get the memory data's current stack position, performing a search if necessary.
 @param L Lua state pointer.
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
 @param L Lua state pointer.
 @param objectIndex Stack position of the object providing the memory. The call
    will replace this object: on success, by some internal state; otherwise, an error.
    If this is a read-style acquire, the object may be a Lua string: if so, and it has
    no callbacks, it is interpreted as a length-sized array.
 @param kind Kind of usage / callbacks: read or write.
 @param params User-supplied parameter; may be `NULL`. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle, CoronaMemoryIsValid
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireBytes( lua_State * L, int objectIndex, CoronaMemoryKind kind, void * params ) CORONA_PUBLIC_SUFFIX;

/**
 Acquire memory from an object, via its callbacks. @see CoronaMemoryCallbacks
 This expects an `ensureSizes()` callback and will invoke it with the expected sizes.
 @param L Lua state pointer.
 @param objectIndex Stack position of the object providing the memory. The call
    will replace this object: on success, by some internal state; otherwise, an error.
    If this is a read-style acquire, the object may be a Lua string: if so, and it has
    no callbacks, it is interpreted as a length-sized array.
 @param kind Kind of usage / callbacks: read or write.
 @param expectedSizes Array of sizes.
 @param sizeCount Count of elements in `expectedSizes`, >= 1.
 @param params User-supplied parameter; may be `NULL`. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle, CoronaMemoryIsValid
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireBytes( lua_State * L, int objectIndex, CoronaMemoryKind kind, const unsigned int * expectedSizes, int sizeCount, void * params ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Get readable memory, following a corresponding acquire.
 @param L Lua state pointer.
 @param memoryHandle Handle to memory data acquired from an object.
 @return On success, the memory; else `NULL`.
*/
CORONA_API
const void * CoronaMemoryGetReadableBytes( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Get writable memory, following a corresponding acquire.
 @param L Lua state pointer.
 @param memoryHandle Handle to memory data acquired from an object.
 @return On success, the memory; else `NULL`.
*/
CORONA_API
void * CoronaMemoryGetWritableBytes( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Get the number of bytes acquired.
 @param L Lua state pointer.
 @param memoryData Handle to memory data acquired from an object.
 @return Count, >= 0.
*/
CORONA_API
unsigned int CoronaMemoryGetByteCount( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

/**
 Get a stride corresponding to the acquired memory.
 @param L Lua state pointer.
 @param memoryHandle Handle to memory data acquired from an object.
 @param strideIndex Index of the stride, between 1 and the stride count, inclusive.
 @param stride Populated on success.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryGetStride( lua_State * L, CoronaMemoryHandle * memoryHandle, int strideIndex, unsigned int * stride ) CORONA_PUBLIC_SUFFIX;

/**
 Get the number of strides corresponding to the acquired memory.
 @param L Lua state pointer.
 @param memoryHandle Handle to memory data acquired from an object.
 @return Count, >= 0.
*/
CORONA_API
unsigned int CoronaMemoryGetStrideCount( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Release the state used by the acquired memory. This is not strictly necessary, since everything is
 on the stack, but allows for some resource-pooling.
 On success, the handle is invalidated and replaced on the stack with `false`.
 @param L Lua state pointer.
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryRelease( lua_State * L, CoronaMemoryHandle * memoryHandle ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Register callbacks for later use.
 @param L Lua state pointer.
 @param callbacks @see CoronaMemoryCallbacks
 @param name If not `NULL`, used for later discovery. @see CoronaMemoryFindCallbacks
 @param userDataFromStack If non-0, the callbacks take ownership of the object on top
 of the stack. If this is a userdata, the callbacks' `userData` member will point to it. @see CoronaMemoryCallbacks
 @param hash If not `NULL`, populated on success. @see CoronaMemoryUnregisterCallbacks
 @return On success, the callbacks' key; else `NULL`.
*/
CORONA_API
void * CoronaMemoryRegisterCallbacks( lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, int userDataFromStack, unsigned int * hash ) CORONA_PUBLIC_SUFFIX;

/**
 Find a set of registered callbacks. @see CoronaMemoryRegisterCallacks
 If multiple sets were registered under the same name, the first one found is chosen.
 @param L Lua state pointer.
 @param name Name of the set.
 @return On success, the callbacks' key (the same as returned during registration); else `NULL`.
*/
CORONA_API
void * CoronaMemoryFindCallbacks( lua_State * L, const char * name ) CORONA_PUBLIC_SUFFIX;

/**
 Remove a set of registered callbacks. @see CoronaMemoryRegisterCallbacks
 @param L Lua state pointer.
 @param name Name of the set.
 @param hash Hash requested when callbacks were registered.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryUnregisterCallbacks( lua_State * L, const char * name, unsigned int hash ) CORONA_PUBLIC_SUFFIX;

/**
 Push the object's callbacks, if any, onto the stack. @see CoronaMemorySetCallbacks, CoronaMemorySetCallbacksByKey
 If unregistered, callbacks must remain on the stack while their key is being used for set operations. @see CoronaMemoryRegisterCallbacks
 @param L Lua state pointer.
 @param objectIndex Stack position of the object.
 @param kind Kind of callbacks: read or write.
 @return On success, the callbacks' key; else `NULL`.
*/
CORONA_API
void * CoronaMemoryGetCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind ) CORONA_PUBLIC_SUFFIX;

/**
 Associate memory callbacks with an object from a set of parameters.
 The object must not already have callbacks.
 @param L Lua state pointer.
 @param objectIndex Stack position of the object.
 @param kind Kind of callbacks: read or write.
 @param callbacks @see CoronaMemoryCallbacks
 @param userDataFromStack If non-0, the callbacks take ownership of the object on top
 of the stack. If this is a userdata, the callbacks' `userData` member will point to it. @see CoronaMemoryCallbacks
 @param hash If not `NULL`, populated on success. @see CoronaMemoryRemoveCallbacks
 @return Non-0 on success.
*/
CORONA_API
void * CoronaMemorySetCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind, const CoronaMemoryCallbacks * callbacks, int userDataFromStack, unsigned int * hash ) CORONA_PUBLIC_SUFFIX;

/**
 Associate memory callbacks with an object from a set that already exists.
 The object must not already have callbacks.
 @param L Lua state pointer.
 @param objectIndex Stack position of the object.
 @param kind Kind of callbacks: read or write. 
 @param key Callbacks' key. @see CoronaMemoryRegisterCallbacks, CoronaMemoryFindCallbacks, CoronaMemoryGetCallbacks
 @param hash If not `NULL`, populated on success. @see CoronaMemoryRemoveCallbacks 
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemorySetCallbacksByKey( lua_State * L, int objectIndex, CoronaMemoryKind kind, void * key, unsigned int * hash ) CORONA_PUBLIC_SUFFIX;

/**
 Remove callbacks associated with the object.
 @param L Lua state pointer.
 @param objectIndex Stack position of the object.
 @param kind Kind of callbacks: read or write.
 @param hash Hash requested when callbacks were set. @see CoronaMemorySetCallbacks, CoronaMemorySetCallbacksByKey
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryRemoveCallbacks( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int hash ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

/**
 Get the minimum power-of-2 alignment of the memory for reading or writing.
 @param L Lua state pointer.
 @param objectIndex Stack position of the object.
 @param kind Kind of alignment: read or write.
 @param alignment Populated on success.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryGetAlignment( lua_State * L, int objectIndex, CoronaMemoryKind kind, unsigned int * alignment ) CORONA_PUBLIC_SUFFIX;

/**
 Is the memory available for reading or writing?
 @param L Lua state pointer.
 @param objectIndex Stack position of the object.
 @param kind Kind of availability: read or write.
 @return Non-0 if available, 0 otherwise.
*/
CORONA_API
int CoronaMemoryIsAvailable( lua_State * L, int objectIndex, CoronaMemoryKind kind ) CORONA_PUBLIC_SUFFIX;

/**
 Is it possible to resize the memory for reading or writing?
 @param L Lua state pointer.
 @param objectIndex Stack position of the object.
 @param kind Kind of memory access: read or write.
 @return Non-0 if resizable, 0 otherwise.
*/
CORONA_API
int CoronaMemoryIsResizable( lua_State * L, int objectIndex, CoronaMemoryKind kind ) CORONA_PUBLIC_SUFFIX;

// ----------------------------------------------------------------------------

#endif // _CoronaMemory_H__
