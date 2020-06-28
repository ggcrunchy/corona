//////////////////////////////////////////////////////////////////////////////
//
// This file is part of the Corona game engine.
// For overview and more information on licensing please refer to README.md 
// Home page: https://github.com/coronalabs/corona
// Contact: support@coronalabs.com
//
//////////////////////////////////////////////////////////////////////////////

#include "Core/Rtt_Build.h"

#include "CoronaGraphics.h"
#include "CoronaLua.h"

#include "Rtt_LuaContext.h"

#include "Display/Rtt_TextureResource.h"
#include "Core/Rtt_SharedPtr.h"
#include "Display/Rtt_TextureFactory.h"
#include "Rtt_Runtime.h"
#include "Display/Rtt_Display.h"
// STEVE CHANGE
#include "Corona/CoronaPluginSupportInternal.h"
#include "Display/Rtt_ShaderData.h"
#include "Display/Rtt_ShaderFactory.h"
#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_Geometry_Renderer.h"
#include "Renderer/Rtt_Renderer.h"
#include "Renderer/Rtt_RenderData.h"

#include <stddef.h>
// /STEVE CHANGE

#include "Display/Rtt_TextureResourceExternalAdapter.h"


CORONA_API
int CoronaExternalPushTexture( lua_State *L, const CoronaExternalTextureCallbacks *callbacks, void* context)
{
	if ( callbacks->size != sizeof(CoronaExternalTextureCallbacks) )
	{
		CoronaLuaError(L, "TextureResourceExternal - invalid binary version for callback structure; size value isn't valid");
		return 0;
	}
	
	if (callbacks == NULL || callbacks->onRequestBitmap == NULL || callbacks->getWidth == NULL || callbacks->getHeight == NULL )
	{
		CoronaLuaError(L, "TextureResourceExternal - bitmap, width and height callbacks are required");
		return 0;
	}
	
	static unsigned int sNextExternalTextureId = 1;
	char filename[30];
	snprintf(filename, 30, "corona://exTex_%u", sNextExternalTextureId++);
	
	Rtt::TextureFactory& factory = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetTextureFactory();
	
	Rtt::SharedPtr< Rtt::TextureResource > ret = factory.FindOrCreateExternal(filename, callbacks, context);
	factory.Retain(ret);
	
	if (ret.NotNull())
	{
		ret->PushProxy( L );
		return 1;
	}
	else
	{
		return 0;
	}
}


CORONA_API
void* CoronaExternalGetUserData( lua_State *L, int index )
{
	return Rtt::TextureResourceExternalAdapter::GetUserData( L, index );
}


CORONA_API
int CoronaExternalFormatBPP(CoronaExternalBitmapFormat format)
{
	switch (format)
	{
		case kExternalBitmapFormat_Mask:
			return 1;
		case kExternalBitmapFormat_RGB:
			return 3;
		default:
			return 4;
	}
}
// STEVE CHANGE
CORONA_API
int CoronaShaderRegisterAttributeSet( lua_State * L, CoronaAttributesHandle * out, const CoronaShaderAttribute * attributes, unsigned int attributeCount )
{
	Rtt_ASSERT_NOT_IMPLEMENTED();

	// Add attributes for later lookup, using the token

	return 0;
}

CORONA_API
int CoronaShaderRegisterCustomization( lua_State * L, const char * name, const CoronaShaderCallbacks * callbacks )
{
	return Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetShaderFactory().RegisterCustomization( name, *callbacks );
}

CORONA_API
int CoronaShaderRegisterProgramMod( int * mod, const char ** details, unsigned int detailsCount )
{
	Rtt_ASSERT_NOT_IMPLEMENTED();

	return 0;
}

CORONA_API
unsigned int CoronaShaderGetProgramModCount()
{
	Rtt_ASSERT_NOT_IMPLEMENTED();

	return 2U;
}

CORONA_API
int CoronaShaderRawDraw( const CoronaShaderHandle shaderHandle, const CoronaRenderDataHandle renderDataHandle, CoronaRendererHandle rendererHandle )
{
	const Rtt::Shader * shader = static_cast< const Rtt::Shader * >( CoronaExtractConstantShader( shaderHandle ) );
	const Rtt::RenderData * renderData = static_cast< const Rtt::RenderData * >( CoronaExtractConstantRenderData( renderDataHandle ) );
	Rtt::Renderer * renderer = static_cast< Rtt::Renderer * >( CoronaExtractRenderer( rendererHandle ) );

	if (shader && renderData && renderer)
	{
		shader->Draw( *renderer, *renderData );

		return 1;
	}

	return 0;
}

CORONA_API
CoronaShaderSourceTransformDetails CoronaShaderGetSourceTransformDetails( const CoronaShaderHandle shaderHandle )
{
	const Rtt::Shader * shader = static_cast< const Rtt::Shader * >( CoronaExtractConstantShader( shaderHandle ) );

	if (shader)
	{
		const Rtt::ShaderData * data = shader->GetData();

		Rtt::SharedPtr< Rtt::ShaderResource > resource( data->GetShaderResource() );
	}

	const CoronaShaderSourceTransformDetails details = {};

	return details;
}

CORONA_API
CoronaRendererBackend CoronaRendererGetBackend( lua_State * )
{
	// TODO: Vulkan, etc.

	#ifdef Rtt_OPENGLES
		return kBackend_OpenGLES;
	#else
		return kBackend_OpenGL;
	#endif
}

static bool
EncodeIndex( void * out, U16 index )
{
	if (index)
	{
		memcpy( out, &index, sizeof( U16 ) );

		return true;
	}

	return false;
}

CORONA_API
int CoronaRendererRegisterBeginFrameOp( lua_State * L, CoronaBeginFrameOpHandle * out, CoronaRendererOp onBeginFrame, void * userData )
{
	const CoronaBeginFrameOpHandle wipe = {};

	*out = wipe;

	return onBeginFrame && EncodeIndex( out, Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddBeginFrameOp( onBeginFrame, userData ) );
}

static U16
DecodeIndex( const void * data )
{
	U16 index;

	memcpy( &index, data, sizeof( U16 ) );

	return index;
}

template<typename T> T
IndexToFlag( U16 index )
{
	return T(1) << (index - 1U);
}

CORONA_API
int CoronaRendererScheduleForNextFrame( CoronaRendererHandle rendererHandle, CoronaBeginFrameOpHandle op, CoronaRenderBeginFrame action )
{
	Rtt::Renderer * renderer = static_cast< Rtt::Renderer *>( CoronaExtractRenderer( rendererHandle ) );
	U16 index = DecodeIndex( &op );

	if (renderer && index)
	{
		U32 flag = IndexToFlag< U32 >( index );

		switch (action)
		{
		case kBeginFrame_Schedule:
			renderer->SetBeginFrameFlags( renderer->GetBeginFrameFlags() | flag );

			break;
		case kBeginFrame_Cancel:
			flag &= ~renderer->GetDoNotCancelFlags();

			renderer->SetBeginFrameFlags( renderer->GetBeginFrameFlags() & ~flag );

			break;
		case kBeginFrame_Establish:
			renderer->SetDoNotCancelFlags( renderer->GetDoNotCancelFlags() | flag );

			break;
		}

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterClearOp( lua_State * L, CoronaClearOpHandle * out, CoronaRendererOp onClear, void * userData )
{
	const CoronaClearOpHandle wipe = {};

	*out = wipe;

	return onClear && EncodeIndex( out, Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddClearOp( onClear, userData ) );
}

CORONA_API
int CoronaRendererEnableClear( CoronaRendererHandle rendererHandle, CoronaClearOpHandle op, int enable )
{
	Rtt::Renderer * renderer = static_cast< Rtt::Renderer *>( CoronaExtractRenderer( rendererHandle ) );
	U16 index = DecodeIndex( &op );

	if (renderer && index)
	{
		U32 flag = IndexToFlag< U32 >( index ), clearFlags = renderer->GetClearFlags();

		renderer->SetClearFlags( enable ? (clearFlags | flag) : (clearFlags & ~flag) );

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterStateOp( lua_State * L, CoronaStateOpHandle * out, CoronaRendererOp onState, void * userData )
{
	const CoronaStateOpHandle wipe = {};
	U16 index = 0U;

	*out = wipe;

	if (onState)
	{
		index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddStateOp( onState, userData );
	}

	else // dummy state (does nothing, but will force a batch)
	{
		static U16 sNoOpIndex;

		lua_pushlightuserdata( L, &sNoOpIndex ); // ..., nonce
		lua_rawget( L, LUA_REGISTRYINDEX ); // ..., used?

		if (!lua_isnil( L, -1 ))
		{
			index = sNoOpIndex;
		}

		else
		{
			index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddStateOp( [](CoronaRendererHandle, void *){}, NULL );

			if (index)
			{
				lua_pushlightuserdata( L, &sNoOpIndex ); // ..., nil, nonce
				lua_pushboolean( L, true ); // ..., nil, nonce, true
				lua_rawset( L, LUA_REGISTRYINDEX ); // ..., nil; registry = { ..., [nonce] = true }

				sNoOpIndex = index;
			}
		}

		lua_pop( L, 1 ); // ...
	}

	return index && EncodeIndex( out, index );
}

CORONA_API
int CoronaRendererSetOperationStateDirty( CoronaRendererHandle rendererHandle, CoronaStateOpHandle op )
{
	Rtt::Renderer * renderer = static_cast< Rtt::Renderer *>( CoronaExtractRenderer( rendererHandle ) );
	U16 index = DecodeIndex( &op );

	if (renderer && index)
	{
		renderer->SetStateFlags( renderer->GetStateFlags() | IndexToFlag< U64 >( index ) );

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterCommand( lua_State * L, CoronaCommandHandle * out, const CoronaCommand * command )
{
	CoronaCommandHandle wipe = {};

	*out = wipe;

	return command && EncodeIndex( out, Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddCustomCommand( *command ) ); // TODO: will fall one short...
}

CORONA_API
int CoronaRendererIssueCommand( CoronaRendererHandle rendererHandle, CoronaCommandHandle command, void * data, unsigned int size )
{
	Rtt::Renderer * renderer = static_cast< Rtt::Renderer *>( CoronaExtractRenderer( rendererHandle ) );
	U16 index = DecodeIndex( &command );
		
	if (renderer && index)
	{
		return renderer->IssueCustomCommand( index - 1U, data, size );
	}

	return 0;
}

CORONA_API
int CoronaRendererSetFrustum( CoronaRendererHandle rendererHandle, const float * viewMatrix, const float * projectionMatrix )
{
	return 0;
}

CORONA_API
unsigned int CoronaGeometryCopyData( void * dst, const CoronaShaderMappingLayout * dstLayout, const void * src, const CoronaShaderMappingLayout * srcLayout )
{
	if (!dst || !dstLayout || !src || !srcLayout)
	{
		return 0U;
	}

	if (!dstLayout->data.stride || dstLayout->data.count != srcLayout->data.count || dstLayout->data.type != srcLayout->data.type)
	{
		return 0U;
	}

	U32 valuesSize = 0U;

	switch (dstLayout->data.type)
	{
	case kAttributeType_Byte:
		valuesSize = 1U;
		break;
	case kAttributeType_Float:
		valuesSize = 4U;
		break;
	default:
		return 0U;
	}

	valuesSize *= dstLayout->data.count;

	U32 srcDatumSize = srcLayout->data.stride ? srcLayout->data.stride : srcLayout->size;

	if (dstLayout->data.offset + valuesSize > dstLayout->data.stride || srcLayout->data.offset + valuesSize > srcDatumSize)
	{
		return 0U;
	}

	U32 n = dstLayout->size / dstLayout->data.stride;

	if (srcLayout->data.stride)
	{
		U32 n2 = srcLayout->size / srcLayout->data.stride;

		if (n2 < n)
		{
			n = n2;
		}
	}

	U8 * dstData = reinterpret_cast< U8 * >( dst );

	for (const U8 * srcData = reinterpret_cast< const U8 * >( src ); n; --n, srcData += srcLayout->data.stride, dstData += dstLayout->data.stride)
	{
		memcpy( dstData, srcData, valuesSize );
	}

	return n;
}

static bool GetLayout( const Rtt::Geometry * geometry, const char * name, CoronaShaderMappingLayout * layout )
{
	if (!name || !name[0])
	{
		return false;
	}

	if (name[1])
	{
		if (strcmp( name, "position" ) == 0)
		{
			layout->data.count = 3U;
			layout->data.offset = offsetof( Rtt::Geometry::Vertex, x );
			layout->data.type = kAttributeType_Float;
		}

		else if (strcmp( name, "texture" ) == 0)
		{
			layout->data.count = 3U;
			layout->data.offset = offsetof( Rtt::Geometry::Vertex, u );
			layout->data.type = kAttributeType_Float;
		}

		else if (strcmp( name, "color" ) == 0)
		{
			layout->data.count = 4U;
			layout->data.offset = offsetof( Rtt::Geometry::Vertex, rs );
			layout->data.type = kAttributeType_Byte;
		}

		else if (strcmp( name, "userdata" ) == 0)
		{
			layout->data.count = 4U;
			layout->data.offset = offsetof( Rtt::Geometry::Vertex, ux );
			layout->data.type = kAttributeType_Float;
		}

		else
		{
			return false;
		}
	}

	else
	{
		U32 offset = 0U;

		switch (*name)
		{
		case 'x':
		case 'y':
		case 'z':
			layout->data.type = kAttributeType_Float;
			layout->data.offset = offsetof( Rtt::Geometry::Vertex, x ) + (*name - 'x');

			break;
		case 'u':
		case 'v':
		case 'q':
			layout->data.type = kAttributeType_Float;
			layout->data.offset = offsetof( Rtt::Geometry::Vertex, u ) + (*name != 'q' ? (*name - 'u') : 2U);

			break;
		case 'a':
			++offset; // n.b. fallthrough...
		case 'b':
			++offset; // ...again...
		case 'g':
			++offset; // ...again...
		case 'r':
			layout->data.type = kAttributeType_Byte;
			layout->data.offset = offsetof( Rtt::Geometry::Vertex, rs ) + offset;

			break;
		default:
			return false;
		}

		layout->data.count = 1U;
	}

	layout->data.stride = sizeof( Rtt::Geometry::Vertex );
	layout->size = sizeof( Rtt::Geometry::Vertex ) * geometry->GetVerticesUsed();

	return true;
}

CORONA_API
void * CoronaGeometryGetMappingFromRenderData( CoronaRenderDataHandle renderDataHandle, const char * name, CoronaShaderMappingLayout * layout )
{
	Rtt::RenderData * renderData = static_cast< Rtt::RenderData * >( CoronaExtractRenderData( renderDataHandle ) );

	if (renderData && GetLayout( renderData->fGeometry, name, layout ))
	{
		return renderData->fGeometry->GetVertexData();
	}

	return NULL;
}

CORONA_API
const void * CoronaGeometryGetMappingFromConstantRenderData( const CoronaRenderDataHandle renderDataHandle, const char * name, CoronaShaderMappingLayout * layout )
{
	const Rtt::RenderData * renderData = static_cast< const Rtt::RenderData * >( CoronaExtractConstantRenderData( renderDataHandle ) );

	if (renderData && GetLayout( renderData->fGeometry, name, layout ))
	{
		return renderData->fGeometry->GetVertexData();
	}

	return NULL;
}

// /STEVE CHANGE
// ----------------------------------------------------------------------------

