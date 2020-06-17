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
#include "Renderer/Rtt_CommandBuffer.h"
#include "Renderer/Rtt_Renderer.h"
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

CORONA_API
int CoronaShaderRegisterAttributes( lua_State * L, CoronaGraphicsToken * token, const CoronaShaderAttribute * attributes, unsigned int attributeCount )
{
	Rtt_ASSERT_NOT_IMPLEMENTED();

	// Add attributes for later lookup, using the token

	return 0;
}

CORONA_API
int CoronaShaderRegisterPolicy( lua_State * L, const char * name, const CoronaShaderCallbacks * callbacks, void * userData )
{
	// Add callbacks for later lookup using the name
	// This is meant for `graphics.defineEffect()`, via say a `policy` field (better name?)

	return 0;
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

template<typename T> int
SetFlagStyleToken( CoronaGraphicsToken * token, CoronaGraphicsTokenType type, U16 index )
{
	token->tokenType = type;

	T flag = 1U << (1 - index);

	memcpy( token->bytes, &flag, sizeof( T ) );

	return 1;
}

CORONA_API
int CoronaRendererRegisterBeginFrameOp( lua_State * L, CoronaGraphicsToken * token, void (*onBeginFrame)(void *), void * userData )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddBeginFrameOp( onBeginFrame, userData );

	if (index)
	{
		return SetFlagStyleToken< U32 >( token, kTokenType_BeginFrameOp, index );
	}

	return 0;
}

template<typename T> T
GetFlagFromToken( const CoronaGraphicsToken * token )
{
	T flag;

	memcpy( &flag, token->bytes, sizeof( flag ) );

	return flag;
}

CORONA_API
int CoronaRendererScheduleForNextFrame( lua_State * L, const CoronaGraphicsToken * token, CoronaRenderBeginFrame action )
{
	if (kTokenType_BeginFrameOp == token->tokenType)
	{
		Rtt::Renderer & renderer = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer();
		U32 flag = GetFlagFromToken< U32 >( token );

		switch (action)
		{
		case kBeginFrame_Schedule:
			renderer.SetBeginFrameFlags( renderer.GetBeginFrameFlags() | flag );

			break;
		case kBeginFrame_Cancel:
			flag &= ~renderer.GetDoNotCancelFlags();

			renderer.SetBeginFrameFlags( renderer.GetBeginFrameFlags() & ~flag );

			break;
		case kBeginFrame_Establish:
			renderer.SetDoNotCancelFlags( renderer.GetDoNotCancelFlags() | flag );

			break;
		}

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterClearOp( lua_State * L, CoronaGraphicsToken * token, void (*onClear)(void *), void * userData )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddClearOp( onClear, userData );

	if (index)
	{
		return SetFlagStyleToken< U32 >( token, kTokenType_ClearOp, index );
	}

	return 0;
}

CORONA_API
int CoronaRendererEnableClear( lua_State * L, const CoronaGraphicsToken * token, int enable )
{
	if (kTokenType_ClearOp == token->tokenType)
	{
		Rtt::Renderer & renderer = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer();
		U32 flag = GetFlagFromToken< U32 >( token ), clearFlags = renderer.GetClearFlags();

		renderer.SetClearFlags( enable ? (clearFlags | flag) : (clearFlags & ~flag) );

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterStateOp( lua_State * L, CoronaGraphicsToken * token, void (*onState)(void *), void * userData )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddStateOp( onState, userData );

	if (index)
	{
		return SetFlagStyleToken< U64 >( token, kTokenType_StateOp, index );
	}

	return 0;
}

CORONA_API
int CoronaRendererSetOperationStateDirty( lua_State * L, const CoronaGraphicsToken * token )
{
	if (kTokenType_StateOp == token->tokenType)
	{
		Rtt::Renderer & renderer = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer();

		renderer.SetStateFlags( renderer.GetStateFlags() | GetFlagFromToken< U64 >( token ) );

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererRegisterCommand( lua_State * L, CoronaGraphicsToken * token, CoronaCustomCommandReader reader, CoronaCustomCommandWriter writer )
{
	U16 index = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().AddCustomCommand( reader, writer );

	if (index)
	{
		token->tokenType = kTokenType_Command;

		--index;

		memcpy( token->bytes, &index, sizeof( U16 ) );

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererIssueCommand( lua_State * L, const CoronaGraphicsToken * token, void * data, unsigned int size )
{
	if (kTokenType_Command == token->tokenType)
	{
		Rtt::Renderer & renderer = Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer();


	// TODO!
	//	Rtt::LuaContext::GetRuntime( L )->GetDisplay().GetRenderer().I

		return 1;
	}

	return 0;
}

CORONA_API
int CoronaRendererSetFrustum( lua_State * L, const float * viewMatrix, const float * projectionMatrix )
{
	return 0;
}

// ----------------------------------------------------------------------------

