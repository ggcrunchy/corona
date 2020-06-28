//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "CoronaMacros.h"
#include "Corona/CoronaPluginSupportInternal.h"

// ----------------------------------------------------------------------------

static CoronaTempStore sRendererStore( "Renderer" );

CORONA_API
void * CoronaExtractRenderer( CoronaRendererHandle rendererHandle )
{
	return sRendererStore.Find( rendererHandle );
}

CORONA_API
const void * CoronaExtractConstantRenderer( const CoronaRendererHandle rendererHandle )
{
	return CoronaExtractRenderer( const_cast< CoronaRendererHandle >( rendererHandle ) );
}

static CoronaTempStore sRenderDataStore( "RenderData" );

CORONA_API
void * CoronaExtractRenderData( CoronaRenderDataHandle renderDataHandle )
{
	return sRenderDataStore.Find( renderDataHandle );
}

CORONA_API
const void * CoronaExtractConstantRenderData( const CoronaRenderDataHandle renderDataHandle )
{
	return CoronaExtractRenderData( const_cast< CoronaRenderDataHandle >( renderDataHandle ) );
}

static CoronaTempStore sShaderStore( "Shader" );

CORONA_API
void * CoronaExtractShader( CoronaShaderHandle shaderHandle )
{
	return sShaderStore.Find( shaderHandle );
}

CORONA_API
const void * CoronaExtractConstantShader( const CoronaShaderHandle shaderHandle )
{
	return CoronaExtractShader( const_cast< CoronaShaderHandle >( shaderHandle ) );
}

static CoronaTempStore sShaderDataStore( "ShaderData" );

CORONA_API
void * CoronaExtractShaderData( CoronaShaderDataHandle shaderDataHandle )
{
	return sShaderDataStore.Find( shaderDataHandle );
}

CORONA_API
const void * CoronaExtractConstantShaderData( const CoronaShaderDataHandle shaderDataHandle )
{
	return CoronaExtractShaderData( const_cast< CoronaShaderDataHandle >( shaderDataHandle ) );
}

static CoronaTempStore sDisplayObjectStore( "DisplayObject" );

enum DisplayObjectType { kBasic, kGroup };

CORONA_API
void * CoronaExtractDisplayObject( CoronaDisplayObjectHandle displayObjectHandle )
{
	return sDisplayObjectStore.Find( displayObjectHandle );
}

CORONA_API
const void * CoronaExtractConstantDisplayObject( const CoronaDisplayObjectHandle displayObjectHandle )
{
	return CoronaExtractDisplayObject( const_cast< CoronaDisplayObjectHandle >( displayObjectHandle ) );
}

CORONA_API
void * CoronaExtractGroupObject( CoronaGroupObjectHandle groupObjectHandle )
{
	int type = -1;

	void * object = sDisplayObjectStore.Find( reinterpret_cast< CoronaDisplayObjectHandle >( groupObjectHandle ), &type );

	if (int( kBasic ) == type)
	{
		return object;
	}

	return NULL;
}

CORONA_API
const void * CoronaExtractConstantGroupObject( const CoronaGroupObjectHandle groupObjectHandle )
{
	return CoronaExtractGroupObject( const_cast< CoronaGroupObjectHandle >( groupObjectHandle ) );
}

CoronaTempStore::BucketRef< CoronaRendererHandle > CoronaInternalStoreRenderer( const void * renderer )
{
	return sRendererStore.Add< CoronaRendererHandle >( const_cast< void * >( renderer ) );
}

CoronaTempStore::BucketRef< CoronaRenderDataHandle > CoronaInternalStoreRenderData( const void * renderData )
{
	return sRenderDataStore.Add< CoronaRenderDataHandle >( const_cast< void * >( renderData ) );
}

CoronaTempStore::BucketRef< CoronaShaderHandle > CoronaInternalStoreShader( const void * shader )
{
	return sShaderStore.Add< CoronaShaderHandle >( const_cast< void * >( shader ) );
}

CoronaTempStore::BucketRef< CoronaShaderDataHandle > CoronaInternalStoreShaderData( const void * shaderData )
{
	return sShaderDataStore.Add< CoronaShaderDataHandle >( const_cast< void * >( shaderData ) );
}

CoronaTempStore::BucketRef< CoronaDisplayObjectHandle > CoronaInternalStoreDisplayObject( const void * displayObject )
{
	return sDisplayObjectStore.Add< CoronaDisplayObjectHandle >( const_cast< void * >( displayObject ) );
}

CoronaTempStore::BucketRef< CoronaGroupObjectHandle > CoronaInternalStoreGroupObject( const void * groupObject )
{
	int extra = kGroup;

	return sDisplayObjectStore.Add< CoronaGroupObjectHandle >( const_cast< void * >( groupObject ), extra );
}

// ----------------------------------------------------------------------------
