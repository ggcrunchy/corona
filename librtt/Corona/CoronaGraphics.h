//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaGraphics_H__
#define _CoronaGraphics_H__

#include "CoronaMacros.h"


#ifdef __cplusplus
extern "C" {
#endif
	typedef struct lua_State lua_State;
#ifdef __cplusplus
}
#endif

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

// C API
// ----------------------------------------------------------------------------

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
int CoronaExternalPushTexture( lua_State *L, const CoronaExternalTextureCallbacks *callbacks, void* userData ) CORONA_PUBLIC_SUFFIX;

/**
 Retrieves userData from TextureResourceExternal on Lua stack
 @param index: location of texture resource on Lua stack
 @return `userData` value texture was created with or
         NULL if stack doesn't contain valid external texture resource at specified index
*/
CORONA_API
void* CoronaExternalGetUserData( lua_State *L, int index ) CORONA_PUBLIC_SUFFIX;

/**
 Helper function, returns how many Bytes Per Pixel bitmap of specified format has
 @param format CoronaExternalBitmapFormat to check
 @return number of bytes per pixel (bpp) of bitmap with specified bitmap format
*/
CORONA_API
int CoronaExternalFormatBPP(CoronaExternalBitmapFormat format) CORONA_PUBLIC_SUFFIX;

// STEVE CHANGE
/**
 Most of the following assumes some familiarity with the source, in particular from librtt\Display and librtt\Renderer:

 TODO: backend, e.g. geometry, program, renderer, texture / FBO stuff, uniforms
*/

typedef enum {
	/**
	TODO
	*/
	kBackend_None,

	/**
	TODO
	*/
	kBackend_OpenGL,

	/**
	TODO
	*/
	kBackend_OpenGLES
} CoronaRendererBackend;

/**
TODO
*/
typedef struct CoronaGraphicsToken {
	unsigned char bytes[8];
	bool hasValue;
} CoronaGraphicsToken;

/**
TODO
*/
typedef struct CoronaShaderCallbacks {
	/**
	Required
	TODO
	*/
	unsigned long size;

	// This seems okay... 

	/**
	Optional
	TODO
	*/
	const char ** (*beginTransform) (const char * source[], unsigned int * n, void * userData);

	/**
	Optional
	TODO
	*/
	void (*endTransform) (const char * transformed[], unsigned int n, void * userData);

	// Argh, this uniform stuff isn't ready to go :P

	/**
	Optional
	TODO
	*/
	unsigned int (*getUniforms) (void * programContext, void * userData);

	/**
	Optional
	TODO
	*/
	void (*resetUniforms) (void * programContext, void * userData);

	/**
	Optional
	TODO
	*/
	unsigned char * (*queryUniform) (void * programContext, unsigned int index, unsigned int * valueCount, unsigned int * sizeInBytes);

	/**
	Optional
	TODO
	*/
	void (*updateUniform) (void * programContext, unsigned int index);

	/**
	Required
	TODO
	*/
	CoronaGraphicsToken attributes;
} CoronaShaderCallbacks;

/**
TODO
*/
typedef struct CoronaShaderAttribute {
	const char * name;
	unsigned int offset;
	unsigned int size;
	unsigned int type;
	unsigned int stride;
	int normalized;
} CoronaShaderAttribute;

CORONA_API
int CoronaShaderRegisterAttributes (CoronaGraphicsToken * token, const CoronaShaderAttribute * attributes, unsigned int attributeCount) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaShaderRegisterPolicy (lua_State * L, const char * name, const CoronaShaderCallbacks * callbacks, void * userData) CORONA_PUBLIC_SUFFIX;

CORONA_API
CoronaRendererBackend CoronaRendererGetBackend () CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererRegisterBeginFrameOp (CoronaGraphicsToken * token, int (*onBeginFrame)(void *), void * userData) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererScheduleForNextFrame (const CoronaGraphicsToken * token, int schedule) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererRegisterClearOp (CoronaGraphicsToken * token, int (*onClear)(void *), void * userData) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererEnableClear (const CoronaGraphicsToken * token, int enable) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererRegisterStateOp (CoronaGraphicsToken * token, int (*onState)(void *), void * userData) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererSetOperationStateDirty (const CoronaGraphicsToken * token) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererRegisterCommand (CoronaGraphicsToken * token, void (*read)(uint8_t * read), int (*write)(uint8_t * bytes, const void * data, uint32_t size)) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererIssueCommand (const CoronaGraphicsToken * token, void * data, uint32_t size) CORONA_PUBLIC_SUFFIX;

CORONA_API
int CoronaRendererSetFrustum (const float * viewMatrix, const float * projectionMatrix) CORONA_PUBLIC_SUFFIX;

// /STEVE CHANGE

#endif // _CoronaGraphics_H__
