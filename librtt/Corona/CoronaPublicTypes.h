//-----------------------------------------------------------------------------
//
// Corona Labs
//
// easing.lua
//
// Code is MIT licensed; see https://www.coronalabs.com/links/code/license
//
//-----------------------------------------------------------------------------

#ifndef _CoronaPublicTypes_H__
#define _CoronaPublicTypes_H__

#include "CoronaMacros.h"

typedef void (*CoronaCommandReader)(const unsigned char *, unsigned int);
typedef void (*CoronaCommandWriter)(unsigned char *, const void *, unsigned int);

typedef struct CoronaCommand {
	CoronaCommandReader fReader;
	CoronaCommandWriter fWriter;
} CoronaCommand;

typedef struct CoronaRenderer * CoronaRendererHandle;
typedef struct CoronaRenderData * CoronaRenderDataHandle;
typedef struct CoronaShader * CoronaShaderHandle;
typedef struct CoronaShaderData * CoronaShaderDataHandle;
typedef struct CoronaDisplayObject * CoronaDisplayObjectHandle;
typedef struct CoronaGroupObject * CoronaGroupObjectHandle;

typedef void (*CoronaRendererOp)(CoronaRendererHandle, void *);

CORONA_API
void * CoronaExtractRenderer( CoronaRendererHandle rendererHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaExtractConstantRenderer( const CoronaRendererHandle rendererHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
void * CoronaExtractRenderData( CoronaRenderDataHandle rendererDataHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaExtractConstantRenderData( const CoronaRenderDataHandle rendererDataHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
void * CoronaExtractShader( CoronaShaderHandle shaderHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaExtractConstantShader( const CoronaShaderHandle shaderHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
void * CoronaExtractShaderData( CoronaShaderDataHandle shaderDataHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaExtractConstantShaderData( const CoronaShaderDataHandle shaderDataHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
void * CoronaExtractDisplayObject( CoronaDisplayObjectHandle displayObjectHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaExtractConstantDisplayObject( const CoronaDisplayObjectHandle displayObjectHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
void * CoronaExtractGroupObject( CoronaGroupObjectHandle groupObjectHandle ) CORONA_PUBLIC_SUFFIX;

CORONA_API
const void * CoronaExtractConstantGroupObject( const CoronaGroupObjectHandle groupObjectHandle ) CORONA_PUBLIC_SUFFIX;

#endif // _CoronaPublicTypes_H__
