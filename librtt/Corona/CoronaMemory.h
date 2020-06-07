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
	typedef struct CoronaMemoryData CoronaMemoryData;
#ifdef __cplusplus
}
#endif
#if 0
/**
 Ennumeration describing format of the bitmap
 Bitmap channels are (left to right) from MSB to LSB. For example RGBA, A is in the least-significant 8 bits
 RGBA format uses premultiplied alpha. This means that if "raw" values of channels are r,g,b and a, red channel should be r*(a/255)
*/
typedef enum {
	/**
	 If not defined, RGBA would be used
	*/
	kExternalBitmapFormat_Undefined = 0, // kExternalBitmapFormat_RGBA would be used

	/**
	 Textures with bitmaps of this format can be used only as masks
	 Alpha, 1 byte per pixel
	 Important: if this format is used, width must be multiple of 4
	*/
	kExternalBitmapFormat_Mask,

	/**
	 RGB, 3 bytes per pixel
	*/
	kExternalBitmapFormat_RGB,

	/**
	 RGBA, 4 bytes per pixel
	 Important: Red, Green and Blue channels must have premultiplied alpha
	 */
	 kExternalBitmapFormat_RGBA,

} CoronaExternalBitmapFormat;


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
typedef struct CoronaMemoryExtra {
	/**
	 If not NULL, the parameter to the acquire operation in progress.
	*/
	void * param; // parameter from acquire, or null

	/**
	 If not NULL, the user data attached to callback when registered or set.
	*/
	void * userData;
} CoronaMemoryExtra;

typedef struct CoronaMemoryCallbacks {
	/**
	 Required
	 When creating instance of this type set this member to `size = sizeof(CoronaMemoryCallbacks)`.
	 This is required for identifying version of API used.
	*/
	unsigned long size;

	void * (*getBytes)(lua_State * L, int objectIndex, unsigned int * count, CoronaMemoryExtra * extra); // required

	int (*canAcquire)(lua_State * L, int objectIndex, CoronaMemoryExtra * extra); // if present, used to filter acquires
	int (*ensureSize)(lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, CoronaMemoryExtra * extra); // if present, we can do 'CoronaMemoryEnsureSizeAndAcquireWritableBytes()'
	int (*getAlignment)(lua_State * L, int objectIndex, unsigned int * alignment, CoronaMemoryExtra * extra);
	int (*getStrides)(lua_State * L, int objectIndex, unsigned int * strideCount, CoronaMemoryExtra * extra); // if present, used to populate strides
} CoronaMemoryCallbacks;


// C API
// ----------------------------------------------------------------------------
#if 0
/**
 Pushes TextureResourseExternal instance onto stack
 @param L Lua state pointer
 @param callbacks set of callbacks used to create texture. @see CoronaExternalTextureCallbacks
 @param userData pointer which would be passed to callbacks
 @return number of values pushed onto Lua stack;
		 1 - means texture was successfully created and is on stack
		 0 - error occurred and nothing was pushed on stack
*/
CORONA_API
int CoronaExternalPushTexture(lua_State *L, const CoronaExternalTextureCallbacks *callbacks, void* userData) CORONA_PUBLIC_SUFFIX;

/**
 Retrieves userData from TextureResourceExternal on Lua stack
 @param index: location of texture resource on Lua stack
 @return `userData` value texture was created with or
		 NULL if stack doesn't contain valid external texture resource at specified index
*/
CORONA_API
void* CoronaExternalGetUserData(lua_State *L, int index) CORONA_PUBLIC_SUFFIX;

/**
 Helper function, returns how many Bytes Per Pixel bitmap of specified format has
 @param format CoronaExternalBitmapFormat to check
 @return number of bytes per pixel (bpp) of bitmap with specified bitmap format
*/
CORONA_API
int CoronaExternalFormatBPP(CoronaExternalBitmapFormat format) CORONA_PUBLIC_SUFFIX;
#endif

/**
 Intended mainly for internal use, in particular by the remaining functions that take a memory data
 handle. Gets the data's current stack position, performing a search if necessary.
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return stack position;
		 0 - means the object was or has become invalid, e.g. was released or removed from the stack
		 otherwise - the up-to-date position
*/
CORONA_API
int CoronaMemoryGetPosition(lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX;

/**
 Pushes a new reference to the memory data onto the stack.
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryPushValue (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object to acquire bytes from; replaced after the call, as described above.
 @param param User-supplied parameter supplied to the various callbacks. May be NULL. @see CoronaMemoryExtra
 @return On success, handle to memory data, else NULL.
*/
CORONA_API
CoronaMemoryData * CoronaMemoryAcquireReadableBytes (lua_State * L, int objectIndex, void * param) CORONA_PUBLIC_SUFFIX; // get object's bytes, returning a handle to read-only data; object is replaced on the

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object to acquire bytes from; replaced after the call, as described above.
 @param param User-supplied parameter supplied to the various callbacks. May be NULL. @see CoronaMemoryExtra
 @return On success, handle to memory data, else NULL.
*/
CORONA_API																								// stack by this data; on failure, an error is placed in this position
CoronaMemoryData * CoronaMemoryAcquireWritableBytes (lua_State * L, int objectIndex, void * param) CORONA_PUBLIC_SUFFIX; // read / write variant

/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object to acquire bytes from; replaced after the call, as described above.
 @param param User-supplied parameter supplied to the various callbacks. May be NULL. @see CoronaMemoryExtra
 @return On success, handle to memory data, else NULL.
*/
CORONA_API
CoronaMemoryData * CoronaMemoryEnsureSizeAndAcquireReadableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param) CORONA_PUBLIC_SUFFIX; // read, but must be able to honor
																																									// some size requirements, resizing if
																																									// possible; sizeCount must be >= 1


/**
 TODO
 @param L Lua state pointer
 @param objectIndex Stack position of the object to acquire bytes from; replaced after the call, as described above.
 @param param User-supplied parameter supplied to the various callbacks. May be NULL. @see CoronaMemoryExtra
 @return On success, handle to memory data, else NULL.
*/
CORONA_API
CoronaMemoryData * CoronaMemoryEnsureSizeAndAcquireWritableBytes (lua_State * L, int objectIndex, const unsigned int * expectedSizes, int sizeCount, void * param) CORONA_PUBLIC_SUFFIX; // read / write, but must be able to honor
																																									// some size requirements, resizing if
																																									// possible; sizeCount must be >= 1

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
void * CoronaMemoryGetBytes (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX; // get acquired memory, containing 'CoronaMemoryGetByteCount()' bytes

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
unsigned int CoronaMemoryGetByteCount (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryGetStride (lua_State * L, CoronaMemoryData * memoryData, int strideIndex, unsigned int * stride) CORONA_PUBLIC_SUFFIX; // point to 'CoronaMemoryGetStrideCount()' strides, or null if 0; if absent, memory is interpreted as size1 x ... x sizeN contiguous bytes

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
unsigned int CoronaMemoryGetStrideCount (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
unsigned int CoronaMemoryGetAlignment (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryRelease (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX; // clean up any acquired resources; not strictly necessary, but allows some caching

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryAddToStash (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX; // the object on top of the stack is added at the end of an array; this is intended for acquires, mainly for internal use: we
																		   // overwrite the original object with our new memory data; it might not be safe to let this object be collected, though; some
																		   // objects might also want additional intermediate objects (and for this reason this is public); some care is taken that these
																		   // objects do become collectable in the case of errors, however

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryRemoveFromStash (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX; // removes last item in said array

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryPopFromStash (lua_State * L, CoronaMemoryData * memoryData) CORONA_PUBLIC_SUFFIX; // like remove, but push item onto stack

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
void * CoronaMemoryRegisterCallbacks (lua_State * L, const CoronaMemoryCallbacks * callbacks, int writable, const char * name, void * userData) CORONA_PUBLIC_SUFFIX; // register for reuse; name allows search

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
void * CoronaMemoryFindRegisteredCallbacks (lua_State * L, const char * name) CORONA_PUBLIC_SUFFIX; // null if absent

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemorySetRegisteredCallbacks (lua_State * L, int objectIndex, void * key) CORONA_PUBLIC_SUFFIX; // attach callbacks to some object, which is then available to be acquired; key comes from 'CoronaMemoryRegisterCallbacks()'

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemorySetCallbacks (lua_State * L, int objectIndex, const CoronaMemoryCallbacks * callbacks, int writable, void * userData) CORONA_PUBLIC_SUFFIX;	// one-off object, so no registration

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryIsReadable (lua_State * L, int objectIndex) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryIsResizable (lua_State * L, int objectIndex, int writable) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryIsWritable (lua_State * L, int objectIndex) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryUsesReader (lua_State * L, int objectIndex, const void * key) CORONA_PUBLIC_SUFFIX;

/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryUsesReaderWriter (lua_State * L, int objectIndex, const void * key) CORONA_PUBLIC_SUFFIX;
/**
 TODO
 @param L Lua state pointer
 @param memoryData Handle to memory data acquired from an object.
 @return 1 on success, else 0
*/
CORONA_API
int CoronaMemoryUsesrWriter (lua_State * L, int objectIndex, const void * key) CORONA_PUBLIC_SUFFIX;

#endif // _CoronaMemory_H__