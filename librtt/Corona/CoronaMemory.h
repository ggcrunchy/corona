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
#if 0
/**
 This structure contains callbacks required for TextureResource's life cycle
 When Corona would require some information about Texture or it's bitmap, a callback would be invoked
 `userData` parameter is passed to all callbacks is same with which TextureResourceExternal was created
 Typical workflow & callbacks descriptions:
  - time to time Corona would request `getWidth()` and `getHeight()` for various calculation
  - when texture's bitmap data is required:
	  * to retrieve raw data pointer `onRequestBitmap()` is called
	  * `onRequestBitmap()` should always return valid pointer
		to `getWidth()*getHeight()*CoronaExternalFormatBPP(getFormat())` bytes
	  * pixels should be aligned row-by-row; first byte of pixel on row `Y` and column `X`:
		`((unsigned char*)onRequestBitmap())[ (Y*getWidth() + X) * CoronaExternalFormatBPP(getFormat())]`
	  * Corona would read data for a short time and call `onReleaseBitmap()` when done
	  * bitmap pointer need not be valid after `onReleaseBitmap()` is called
	  * `onRequestBitmap()` (and consequent `onReleaseBitmap()`) may be called several times when data access is required
  - `getFormat()` should return bitmap format; if NULL RGBA would be used
	  * if `kExternalBitmapFormat_Mask` is returned, texture would be treated as mask
  - `onFinalize()` if present, would be called when texture is no longer needed. Usually happens when `texture:releaseSelf()`
	is called and all objects using texture are destroyed. Also called when app is shutting down or restarted
  - `onGetField()` is used when user queries texture for unknown field from Lua. Returned number
	must be a number of values pushed on Lua stack

 In order to create external bitmap you must provide width, height and bitmap callbacks
 all other are optional and will be ignored if set to NULL
*/
typedef struct CoronaExternalTextureCallbacks
{
	/**
	 Required
	 When creating instance of this type set this member to `size = sizeof(CoronaExternalTextureCallbacks)`.
	 This is required for identifying version of API used.
	*/
	unsigned long size;

	/**
	 Required
	 called when Texture bitmap's width is required
	 @param userData Pointer passed to CoronaExternalPushTexture
	 @return The width of Texture's bitmap; Important: if it is a Mask, width should be a multiple of 4
	*/
	unsigned int (*getWidth)(void* userData);

	/**
	 Required
	 called when Texture bitmap's height is required
	 @param userData Pointer passed to CoronaExternalPushTexture
	 @return The width of Texture's height
	*/
	unsigned int (*getHeight)(void* userData);

	/**
	 Required
	 called when Texture bitmap's data is required. Always followed by @see onReleaseBitmap call.
	 @param userData Pointer passed to CoronaExternalPushTexture
	 @return Valid pointer to data containing bitmap information. Corona expects bitmap data to be row-by-row array of pixels
			 starting from top of the image, each pixel represented by `bpp = CoronaExternalFormatBPP(getFormat())` bytes.
			 Each channel use 1 byte and ordered same as format name, left to right. So, with RGBA format, R index is 0
			 Overall size of memory must me at least `getWidth()*getHeight()*bpp`
			 Accessing left most (R in RGBA) value of bitmap could be written as
			 `((unsigned char*)onRequestBitmap())[ (Y*getWidth() + X) * CoronaExternalFormatBPP(getFormat()) ]`
			 RGBA format (default) uses premultiplied alpha
	*/
	const void* (*onRequestBitmap)(void* userData);

	/**
	 Optional
	 Called when Texture bitmap's data is no longer required.
	 After this callback is invoked, pointer returned by `onRequestBitmap` need not longer be valid
	 @param userData Pointer passed to CoronaExternalPushTexture
	*/
	void (*onReleaseBitmap)(void* userData);

	/**
	 Optional
	 called when Texture bitmap's format is required
	 @param userData Pointer passed to CoronaExternalPushTexture
	 @return One of the CoronaExternalBitmapFormat entries. Default format is RGBA (kExternalBitmapFormat_RGBA)
	*/
	CoronaExternalBitmapFormat (*getFormat)(void* userData);

	/**
	 Optional
	 Called when TextureResource is about to be destroyed
	 After this callback is invoked, no callbacks or returned bitmap pointers would be accessed
	 @param userData Pointer passed to CoronaExternalPushTexture
	*/
	void (*onFinalize)(void *userData);     // optional; texture will not be used again

	/**
	 Optional
	 Called when unknown property of Texture is requested from Lua
	 @param L Lua state pointer
	 @param field String containing name of requested field
	 @param userData Pointer passed to CoronaExternalPushTexture
	 @return number of values pushed on Lua stack
	*/
	int (*onGetField)(lua_State *L, const char *field, void* userData);   // optional; called Lua texture property lookup
} CoronaExternalTextureCallbacks;
#endif

/**
 Enumeration indicating various policies on acquired data.
*/
typedef enum {
	/**
	 An invalid state.
	*/
	kMemoryAcquireKind_Undefined,

	/**
	 The data is only meant to be read.
	*/
	kMemoryAcquireKind_ReadOnly,

	/**
	 The data may be both read and written.
	*/
	kMemoryAcquireKind_ReadWrite,

	/**
	 The data is only meant to be written.
	*/
	kMemoryAcquireKind_WriteOnly

} CoronaMemoryAcquireKind;

/**
 Extra information supplied to the various callbacks.
*/
typedef struct CoronaMemoryExtra {
	/**
	 If present, the parameter to the acquire operation in progress.
	*/
	void * param; // parameter from acquire, or null

	/**
	 If present, the user data attached to callback when registered or set.
	*/
	void * userData;
} CoronaMemoryExtra;

/**
 TODO
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

	// The following are traversed in order and called if present, stopping if one of them fails:

	/**
	 Optional
	 The acquire attempt will be aborted if this returns 0.
	*/
	int (*canAcquire)(lua_State * L, int objectIndex, CoronaMemoryExtra * extra);

	/**
	 Optional
	 If requested, attempts to conform the object's data to the expected sizes.
	*/
	int (*ensureSizes)(lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, CoronaMemoryExtra * extra);

	/**
	 Required
	 On success, points to the memory and populates 'byteCount' with the addressable size.
	*/
	void * (*getBytes)(lua_State * L, int objectIndex, unsigned int * byteCount, int writable, CoronaMemoryExtra * extra);

	/**
	 Optional
	 If this returns 0, 'strideCount' is populated with some count and the corresponding numbers pushed on the stack.
	*/
	int (*getStrides)(lua_State * L, int objectIndex, unsigned int * strideCount, CoronaMemoryExtra * extra);

	// If any of the above left new items on the stack before failing, i.e. returning NULL or 0, the topmost item will be
	// interpreted as an error and replace the object at 'objectIndex'. Otherwise, in said failure case, an appropriate
	// default error will replace the object instead.
} CoronaMemoryCallbacks;


// C API
// ----------------------------------------------------------------------------

/**
 Handle to acquired memory, used by several operations.
 These are internal fields and should not be written. However, they may be read: if the position is 0
 or the data NULL, the handle is invalid; it is enough to check one or the other. Users should only
 need to do this to check the success of an acquire call.
*/
typedef struct CoronaMemoryHandle {
	int lastKnownStackPosition;
	struct CoronaMemoryData * data;
} CoronaMemoryHandle;

/**
 Intended mainly for internal use, in particular by the remaining functions that take a CoronaMemoryData.
 Gets the data's current stack position, performing a search if necessary.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Stack position:
		 0 - means the object was or has become invalid, e.g. was released or removed from the stack
		 otherwise - the up-to-date position
*/
CORONA_API
int CoronaMemoryGetPosition(lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 Acquire memory from an object that may be read, preferentially via read-only callbacks, else read-write ones. @see CoronaMemoryCallbacks
 @param L Lua state pointer
 @param objectIndex Stack position of the object providing the memory. (This object may be a Lua string; if it does not
			already have one of the callbacks, a reader is provided that interprets it as a length-sized array.) On
			success, the object will be replaced by some internal state; otherwise, an error will takes its place.
 @param param User-supplied parameter; may be NULL. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireReadableBytes (lua_State * L, int objectIndex, void * param) CORONA_PUBLIC_SUFFIX;

/**
 Acquire memory from an object that may be written, preferentially via write-only callbacks, else read-write ones. @see CoronaMemoryCallbacks
 @param L Lua state pointer
 @param objectIndex Stack position of the object providing the memory. On success, the object will be replaced by some
			internal state; otherwise, an error will takes its place.
 @param param User-supplied parameter; may be NULL. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryAcquireWritableBytes (lua_State * L, int objectIndex, void * param) CORONA_PUBLIC_SUFFIX;

/**
 Acquire memory from an object that may be read, preferentially via read-only callbacks, else read-write ones. @see CoronaMemoryCallbacks
			This requires an 'ensureSizes()' callback and will invoke this with the expected sizes.
 @param L Lua state pointer
 @param objectIndex Stack position of the object providing the memory. On success, the object will be replaced by some
			internal state; otherwise, an error will takes its place.
 @param expectedSizes Array of sizes.
 @param sizeCount Size of array, >= 1.
 @param param User-supplied parameter; may be NULL. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireReadableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param) CORONA_PUBLIC_SUFFIX;


/**
 Acquire memory from an object that may be written, preferentially via write-only callbacks, else read-write ones. @see CoronaMemoryCallbacks
			This requires an 'ensureSizes()' callback and will invoke this with the expected sizes.
 @param L Lua state pointer
 @param objectIndex Stack position of the object providing the memory. On success, the object will be replaced by some
			internal state; otherwise, an error will takes its place.
 @param expectedSizes Array of sizes.
 @param sizeCount Size of array, >= 1.
 @param param User-supplied parameter; may be NULL. @see CoronaMemoryExtra
 @return Handle to memory data, with valid contents on success. @see CoronaMemoryHandle
*/
CORONA_API
CoronaMemoryHandle CoronaMemoryEnsureSizeAndAcquireWritableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param) CORONA_PUBLIC_SUFFIX;

/**
 Get readable memory, after a corresponding acquire.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return On success, the memory; else NULL.
*/
CORONA_API
const void * CoronaMemoryGetReadableBytes (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 Get writable memory, after a corresponding acquire.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return On success, the memory; else NULL.
*/
CORONA_API
void * CoronaMemoryGetWritableBytes (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 Get the number of bytes acquired.
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return Count, >= 0.
*/
CORONA_API
unsigned int CoronaMemoryGetByteCount (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 Gets a stride corresponding to the acquired memory.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @param strideIndex Index between 1 and the stride count, inclusive, of the stride.
 @stride On success, populated with the requested stride.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryGetStride (lua_State * L, CoronaMemoryHandle * memoryHandle, int strideIndex, unsigned int * stride) CORONA_PUBLIC_SUFFIX;

/**
 Get the number of strides corresponding to the acquired memory.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Count, >= 0.
*/
CORONA_API
unsigned int CoronaMemoryGetStrideCount (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 Releases the state used by the acquired memory. This is not strictly necessary--only the stack is used internally to maintain
 references, so garbage collection will account for everything--but allows for some resource-pooling.
 On success, the handle is invalidated and replaced on the stack with the boolean 'false'.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryRelease (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 The object on top of the stack is appended to an array; this operation is intended for acquires, mainly for internal use: we
 overwrite the original object with our new memory data, but it might not be safe to let the object be collected; furthermore,
 some objects might want intermediate components, with the same collectability concerns.
 In the event of an error, the underlying array will be cleaned up.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryAddToStash (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 Removes the last item from the memory's stash array.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryRemoveFromStash (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 Removes the last item from the memory's stash array, pushing it on the stack.
 @param L Lua state pointer
 @param memoryHandle Handle to memory data acquired from an object.
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemoryPopFromStash (lua_State * L, CoronaMemoryHandle * memoryHandle) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param callbacks
 @param name
 @param userData
 @return Non-0 on success.
*/
CORONA_API
void * CoronaMemoryRegisterCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, const char * name, void * userData) CORONA_PUBLIC_SUFFIX; // register for reuse; name allows search

/**
 TODO
 @param L Lua state pointer
 @param name
 @return Non-0 on success.
*/
CORONA_API
void * CoronaMemoryFindRegisteredCallbacks (lua_State * L, const char * name) CORONA_PUBLIC_SUFFIX; // null if absent

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param kind
 @param key
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemorySetRegisteredCallbacks (lua_State * L, int objectIndex, CoronaMemoryAcquireKind kind, void * key) CORONA_PUBLIC_SUFFIX; // attach callbacks to some object, which is then available to be acquired; key comes from 'CoronaMemoryRegisterCallbacks()'

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param callbacks
 @param kind
 @param userData
 @return Non-0 on success.
*/
CORONA_API
int CoronaMemorySetCallbacks (lua_State * L, int objectIndex, const CoronaMemoryCallbacks * callbacks, CoronaMemoryAcquireKind kind, void * userData) CORONA_PUBLIC_SUFFIX;	// one-off object, so no registration

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param writable
 @param alignment
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryGetAlignment (lua_State * L, int objectIndex, int writable, unsigned int * alignment) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @return Non-0 if readable, 0 otherwise.
*/
CORONA_API
int CoronaMemoryIsReadable (lua_State * L, int objectIndex) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @param writable
 @return Non-0 if resizable, 0 otherwise.
*/
CORONA_API
int CoronaMemoryIsResizable (lua_State * L, int objectIndex, int writable) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @return Non-0 if writable, 0 otherwise.
*/
CORONA_API
int CoronaMemoryIsWritable (lua_State * L, int objectIndex) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @return Key on success, else NULL.
*/
CORONA_API
void * CoronaMemoryGetReadOnlyKey (lua_State * L, int objectIndex) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @return Key on success, else NULL.
*/
CORONA_API
void * CoronaMemoryGetReadWriteKey (lua_State * L, int objectIndex) CORONA_PUBLIC_SUFFIX;
/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object.
 @return Key on success, else NULL.
*/
CORONA_API
void * CoronaMemoryGetWriteOnlyKey (lua_State * L, int objectIndex) CORONA_PUBLIC_SUFFIX;

#endif // _CoronaMemory_H__