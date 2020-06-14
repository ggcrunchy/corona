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

struct DisplayObjectParams {
    void (*beforeDraw) ();
    void (*afterDraw) ();
};

CORONA_API
int CoronaObjectsPushGroup (lua_State *L, const DisplayObjectParams * params) CORONA_PUBLIC_SUFFIX;

/**
 Retrieves userData from TextureResourceExternal on Lua stack
 @param index: location of texture resource on Lua stack
 @return `userData` value texture was created with or
         NULL if stack doesn't contain valid external texture resource at specified index
*/

struct ShapeParams {
    DisplayObjectParams inherited;
    int (*canHitTest) ();
    int (*canDraw) ();
};

CORONA_API
void* CoronaObjectsPushRect (lua_State *L, const ShapeParams * params) CORONA_PUBLIC_SUFFIX;

/**
 Helper function, returns how many Bytes Per Pixel bitmap of specified format has
 @param format CoronaExternalBitmapFormat to check
 @return number of bytes per pixel (bpp) of bitmap with specified bitmap format
*/

struct SnapshotParams {
    DisplayObjectParams inherited;
    // TODO: render format
};

CORONA_API
int CoronaObjectsPushSnapshot (lua_State * L, const SnapshotParams * params) CORONA_PUBLIC_SUFFIX;

// 

#endif // _CoronaGraphics_H__
